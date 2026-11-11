/*
 * hybrid_server.c - TLS 1.3 test server for the hybrid authentication benchmark.
 *
 * Holds two certificates and lets OpenSSL pick one from the client's
 * signature_algorithms:
 *   - RSA-3072      for legacy clients (the baseline, like the hackathon server)
 *   - ECDSA P-256   for hybrid clients
 * When the request carries "PQ-Auth: ML-DSA-44" or "PQ-Auth: ML-DSA-65", the
 * response also carries an ML-DSA signature over a TLS exporter value. The
 * exporter is derived from this session's handshake transcript, so the
 * signature cannot be replayed into another session. A hybrid client accepts
 * only if both ECDSA (in the TLS handshake) and ML-DSA (over the exporter) verify.
 *
 * Signature encoding (request header "PQ-Auth-Encoding"):
 *   base64 (default)  header  "PQ-Auth-Signature: ML-DSA-xx <base64>"
 *   binary            header  "PQ-Auth-Signature-Length: <n>", and the body
 *                     starts with the n raw signature bytes, then the JSON.
 *
 * Resumed sessions (TLS 1.3 PSK with fresh (EC)DHE/ML-KEM key exchange) were
 * established by a fully authenticated handshake, so no new signature is sent;
 * the response says "PQ-Auth: resumed".
 *
 * Usage: ./hybrid_server [port] [key_dir]   (defaults: 8444, keys)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/objects.h>

#define EXPORTER_LABEL "EXPORTER-pq-hybrid-auth"
#define SIG_PREFIX "hybrid-auth-v1"
#define EKM_LEN 32
#define MAX_REQUEST 8192
#define MAX_SIG 8192

static EVP_PKEY *mldsa44, *mldsa65;

static EVP_PKEY *load_private_key(const char *dir, const char *name)
{
    char path[512];
    EVP_PKEY *pkey;
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if ((f = fopen(path, "r")) == NULL) {
        perror(path);
        return NULL;
    }
    pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
    fclose(f);
    return pkey;
}

static int load_cert_and_key(SSL_CTX *ctx, const char *dir, const char *name)
{
    char cert[512], key[512];

    snprintf(cert, sizeof(cert), "%s/%s_cert.pem", dir, name);
    snprintf(key, sizeof(key), "%s/%s_key.pem", dir, name);
    if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "Cannot load %s / %s\n", cert, key);
        ERR_print_errors_fp(stderr);
        return 0;
    }
    return 1;
}

/* ML-DSA signature over SIG_PREFIX || exporter into sig. Returns its length, 0 on error. */
static size_t sign_exporter(SSL *ssl, EVP_PKEY *key, unsigned char *sig, size_t sig_max)
{
    unsigned char msg[sizeof(SIG_PREFIX) - 1 + EKM_LEN];
    size_t sig_len = sig_max;
    EVP_MD_CTX *mctx;
    int ok;

    memcpy(msg, SIG_PREFIX, sizeof(SIG_PREFIX) - 1);
    if (SSL_export_keying_material(ssl, msg + sizeof(SIG_PREFIX) - 1, EKM_LEN,
                                   EXPORTER_LABEL, strlen(EXPORTER_LABEL),
                                   NULL, 0, 0) != 1)
        return 0;
    mctx = EVP_MD_CTX_new();
    ok = mctx != NULL &&
         EVP_DigestSignInit_ex(mctx, NULL, NULL, NULL, NULL, key, NULL) == 1 &&
         EVP_DigestSign(mctx, sig, &sig_len, msg, sizeof(msg)) == 1;
    EVP_MD_CTX_free(mctx);
    if (!ok) {
        ERR_print_errors_fp(stderr);
        return 0;
    }
    return sig_len;
}

static void handle(SSL *ssl)
{
    static char req[MAX_REQUEST + 1];
    static unsigned char sig[MAX_SIG];
    static char sig_b64[4 * MAX_SIG / 3 + 4];
    static char resp[16384];
    char body[512], pq_header[64 + sizeof(sig_b64)];
    const char *group, *sigalg = "unknown", *pq_name = NULL, *pq_status;
    EVP_PKEY *pq_key = NULL;
    size_t total = 0, sig_len = 0, body_len, head_len;
    int n, nid = 0, binary, resumed;

    if (SSL_accept(ssl) != 1) {
        fprintf(stderr, "TLS handshake failed\n");
        ERR_print_errors_fp(stderr);
        return;
    }
    while (total < MAX_REQUEST &&
           (n = SSL_read(ssl, req + total, (int)(MAX_REQUEST - total))) > 0) {
        total += (size_t)n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n"))
            break;
    }
    req[total] = '\0';

    group = SSL_get0_group_name(ssl);
    resumed = SSL_session_reused(ssl);
    if (resumed) {
        sigalg = "none (resumed)";
    } else if (!SSL_get0_signature_name(ssl, &sigalg) || sigalg == NULL) {
        sigalg = "unknown";
        if (SSL_get_signature_type_nid(ssl, &nid) && nid != 0)
            sigalg = OBJ_nid2sn(nid);
    }
    if (strstr(req, "PQ-Auth: ML-DSA-44") != NULL) {
        pq_name = "ML-DSA-44";
        pq_key = mldsa44;
    } else if (strstr(req, "PQ-Auth: ML-DSA-65") != NULL) {
        pq_name = "ML-DSA-65";
        pq_key = mldsa65;
    }
    binary = strstr(req, "PQ-Auth-Encoding: binary") != NULL;

    pq_header[0] = '\0';
    pq_status = "none";
    if (pq_name != NULL && resumed) {
        pq_status = "resumed";
        snprintf(pq_header, sizeof(pq_header), "PQ-Auth: resumed\r\n");
    } else if (pq_name != NULL && (sig_len = sign_exporter(ssl, pq_key, sig, sizeof(sig))) > 0) {
        pq_status = pq_name;
        if (binary) {
            snprintf(pq_header, sizeof(pq_header),
                     "PQ-Auth-Signature-Length: %zu\r\n", sig_len);
        } else {
            EVP_EncodeBlock((unsigned char *)sig_b64, sig, (int)sig_len);
            snprintf(pq_header, sizeof(pq_header),
                     "PQ-Auth-Signature: %s %s\r\n", pq_name, sig_b64);
        }
    }
    if (!binary)
        sig_len = 0;    /* signature travels in the header, not the body */

    body_len = (size_t)snprintf(body, sizeof(body),
                                "{\"key_exchange\":\"%s\",\"server_signature\":\"%s\","
                                "\"pq_signature\":\"%s\",\"resumed\":%s}\n",
                                group ? group : "unknown", sigalg, pq_status,
                                resumed ? "true" : "false");
    head_len = (size_t)snprintf(resp, sizeof(resp),
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: application/json\r\n"
                                "%s"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                                pq_header, sig_len + body_len);
    memcpy(resp + head_len, sig, sig_len);
    memcpy(resp + head_len + sig_len, body, body_len);
    SSL_write(ssl, resp, (int)(head_len + sig_len + body_len));

    printf("%s + %s + PQ %s%s\n", group ? group : "?", sigalg, pq_status,
           sig_len ? " (binary)" : "");
    fflush(stdout);
    SSL_shutdown(ssl);
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8444;
    const char *dir = argc > 2 ? argv[2] : "keys";
    struct sockaddr_in6 addr;
    SSL_CTX *ctx;
    int lfd, one = 1, zero = 0;

    ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == NULL)
        return 1;
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set1_groups_list(ctx, "X25519MLKEM768:X25519");
    if (!load_cert_and_key(ctx, dir, "rsa") || !load_cert_and_key(ctx, dir, "ecdsa"))
        return 1;
    if ((mldsa44 = load_private_key(dir, "mldsa44_key.pem")) == NULL ||
        (mldsa65 = load_private_key(dir, "mldsa65_key.pem")) == NULL)
        return 1;

    lfd = socket(AF_INET6, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons((unsigned short)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(lfd, 16) != 0) {
        perror("bind/listen");
        return 1;
    }
    printf("hybrid_server on port %d (%s), OpenSSL %s\n", port, dir,
           OpenSSL_version(OPENSSL_VERSION_STRING));
    fflush(stdout);

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        SSL *ssl;

        if (fd < 0)
            continue;
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        handle(ssl);
        SSL_free(ssl);
        close(fd);
    }
}
