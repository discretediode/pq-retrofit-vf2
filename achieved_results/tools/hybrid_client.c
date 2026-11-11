/*
 * hybrid_client.c - benchmark client for legacy vs hybrid TLS 1.3 authentication.
 *
 * auth = rsa       baseline: server authenticates with RSA-3072 (rsa_pss_rsae_sha256)
 * auth = hybrid65  server authenticates with ECDSA P-256 in the TLS handshake AND
 * auth = hybrid44  with an ML-DSA-65 / ML-DSA-44 signature over a TLS exporter
 *                  value, sent in the HTTP response (no extra round trip).
 *                  Both must verify. ("hybrid" means hybrid65.)
 *
 * Options (after key_dir):
 *   binary   ask for the ML-DSA signature as raw bytes instead of base64
 *   certext  v2: ML-DSA-44 signature arrives in a TLS 1.3 Certificate extension, over
 *            SHA-256(ClientHello || ServerHello), and is verified during the handshake
 *   par      with certext: verify ML-DSA on a worker thread (second core) while OpenSSL
 *            verifies the ECDSA CertificateVerify on this thread; join before accepting
 *   resume   reuse the session ticket of the previous, fully verified connection
 *            (TLS 1.3 PSK with fresh key exchange; no signatures on resumption)
 *
 * The server is really authenticated: its certificate is pinned as the only
 * trust anchor (SSL_VERIFY_PEER) and the ML-DSA public key is pinned.
 *
 * Measures per connection: TCP connect, TLS handshake, end-to-end latency
 * (TCP connect start -> verified response), ML-DSA verify time, bytes on the
 * wire. Prints medians and the process's peak memory. With "resume", the
 * medians cover the resumed connections and the first (full) one is shown apart.
 * TRACE=1 in the environment prints a per-message handshake timeline;
 * RAW=1 prints one "RAW,run,tcp_ms,handshake_ms,e2e_ms,resumed" line per connection.
 *
 * Usage:   ./hybrid_client host port group auth [runs] [key_dir] [binary] [resume]
 * Example: ./hybrid_client 192.168.2.1 8444 X25519 rsa 50
 *          ./hybrid_client 192.168.2.1 8444 X25519MLKEM768 hybrid44 50 keys binary
 */
#define _GNU_SOURCE     /* memmem() on glibc */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/objects.h>

#define EXPORTER_LABEL "EXPORTER-pq-hybrid-auth"
#define SIG_PREFIX "hybrid-auth-v1"
#define EKM_LEN 32
#define MAX_RESPONSE 65536
#define MAX_SIG 8192
#define SIG_PREFIX_V2 "hybrid-auth-v2"
#define PQ_AUTH_EXT 0xFE20      /* private-use extension number for the v2 signature */
#define MAX_HELLO 8192

struct byte_count {
    size_t rx, tx;
};

struct result {
    double tcp_ms, handshake_ms, e2e_ms, verify_ms, wait_ms;
    size_t hs_rx, hs_tx, rx, tx;
    int resumed;
};

static const char *host, *port, *pq_name;
static EVP_PKEY *mldsa_pub;
static int hybrid, binary, resume, certext, parallel;
static SSL_SESSION *last_session;
static double trace_t0;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static long count_bytes(BIO *b, int oper, const char *argp, size_t len, int argi,
                        long argl, int ret, size_t *processed)
{
    struct byte_count *c = (struct byte_count *)BIO_get_callback_arg(b);

    (void)argp; (void)len; (void)argi; (void)argl;
    if (ret > 0 && processed != NULL) {
        if (oper == (BIO_CB_READ | BIO_CB_RETURN))
            c->rx += *processed;
        else if (oper == (BIO_CB_WRITE | BIO_CB_RETURN))
            c->tx += *processed;
    }
    return ret;
}

/* ---- v2: ML-DSA signature in the Certificate message ---------------------- */

static struct {
    unsigned char ch[MAX_HELLO], sh[MAX_HELLO], sig[MAX_SIG];
    unsigned char msg[sizeof(SIG_PREFIX_V2) - 1 + 32];
    size_t ch_len, sh_len, sig_len;
    int got_sig, verified, busy;
    double verify_ms;           /* time the ML-DSA verification itself took */
} v2;

static pthread_mutex_t v2_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t v2_start = PTHREAD_COND_INITIALIZER, v2_done = PTHREAD_COND_INITIALIZER;

static int mldsa_verify(const unsigned char *msg, size_t msg_len,
                        const unsigned char *sig, size_t sig_len)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    int ok = mctx != NULL &&
             EVP_DigestVerifyInit_ex(mctx, NULL, NULL, NULL, NULL, mldsa_pub, NULL) == 1 &&
             EVP_DigestVerify(mctx, sig, sig_len, msg, msg_len) == 1;

    EVP_MD_CTX_free(mctx);
    return ok;
}

static double now_ms(void);

static void v2_verify_now(void)
{
    double t = now_ms();

    v2.verified = mldsa_verify(v2.msg, sizeof(v2.msg), v2.sig, v2.sig_len);
    v2.verify_ms = now_ms() - t;
}

/* Persistent worker thread: verifies the ML-DSA signature when woken. */
static void *v2_worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&v2_mu);
    for (;;) {
        while (!v2.busy)
            pthread_cond_wait(&v2_start, &v2_mu);
        pthread_mutex_unlock(&v2_mu);
        v2_verify_now();
        pthread_mutex_lock(&v2_mu);
        v2.busy = 0;
        pthread_cond_signal(&v2_done);
    }
    return NULL;
}

/* ClientHello: send the (empty) extension to ask the server for the v2 signature. */
static int v2_add(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char **out,
                  size_t *outlen, X509 *x, size_t chainidx, int *al, void *arg)
{
    (void)s; (void)ext_type; (void)x; (void)chainidx; (void)al; (void)arg;
    if (context != SSL_EXT_CLIENT_HELLO)
        return 0;
    *out = NULL;
    *outlen = 0;
    return 1;
}

/* Certificate: take the signature and start verifying it (here or on the worker). */
static int v2_parse(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *in,
                    size_t inlen, X509 *x, size_t chainidx, int *al, void *arg)
{
    unsigned char hello[2 * MAX_HELLO];
    unsigned int hlen = 0;

    (void)s; (void)ext_type; (void)x; (void)arg;
    if (context != SSL_EXT_TLS1_3_CERTIFICATE || chainidx != 0)
        return 1;
    if (inlen == 0 || inlen > MAX_SIG || v2.ch_len == 0 || v2.sh_len == 0 || v2.got_sig) {
        *al = SSL_AD_DECODE_ERROR;
        return 0;
    }
    memcpy(v2.sig, in, inlen);
    v2.sig_len = inlen;
    memcpy(hello, v2.ch, v2.ch_len);
    memcpy(hello + v2.ch_len, v2.sh, v2.sh_len);
    memcpy(v2.msg, SIG_PREFIX_V2, sizeof(SIG_PREFIX_V2) - 1);
    if (EVP_Digest(hello, v2.ch_len + v2.sh_len, v2.msg + sizeof(SIG_PREFIX_V2) - 1, &hlen,
                   EVP_sha256(), NULL) != 1) {
        *al = SSL_AD_INTERNAL_ERROR;
        return 0;
    }
    v2.got_sig = 1;
    if (parallel) {
        pthread_mutex_lock(&v2_mu);
        v2.busy = 1;
        pthread_cond_signal(&v2_start);
        pthread_mutex_unlock(&v2_mu);
        return 1;               /* OpenSSL goes on to verify ECDSA meanwhile */
    }
    v2_verify_now();
    if (!v2.verified)
        *al = SSL_AD_DECRYPT_ERROR;
    return v2.verified;
}

/* Wait for the worker (parallel mode); returns the verification result. */
static int v2_join(void)
{
    pthread_mutex_lock(&v2_mu);
    while (v2.busy)
        pthread_cond_wait(&v2_done, &v2_mu);
    pthread_mutex_unlock(&v2_mu);
    return v2.verified;
}

static void trace_msg(int write_p, int version, int content_type, const void *buf,
                      size_t len, SSL *ssl, void *arg);

/* Message callback: record the hellos for v2 and/or print the TRACE timeline. */
static void msg_cb(int write_p, int version, int content_type, const void *buf,
                   size_t len, SSL *ssl, void *arg)
{
    const unsigned char *m = buf;

    if (certext && content_type == SSL3_RT_HANDSHAKE && len > 0 && len <= MAX_HELLO) {
        if (write_p && m[0] == SSL3_MT_CLIENT_HELLO && v2.ch_len == 0) {
            memcpy(v2.ch, m, len);
            v2.ch_len = len;
        } else if (!write_p && m[0] == SSL3_MT_SERVER_HELLO && v2.sh_len == 0) {
            memcpy(v2.sh, m, len);
            v2.sh_len = len;
        }
    }
    if (getenv("TRACE") != NULL)
        trace_msg(write_p, version, content_type, buf, len, ssl, arg);
}

/* TRACE=1: print when each TLS handshake message is sent/received. */
static void trace_msg(int write_p, int version, int content_type, const void *buf,
                      size_t len, SSL *ssl, void *arg)
{
    static const char *names[] = {
        [1] = "ClientHello", [2] = "ServerHello", [4] = "NewSessionTicket",
        [8] = "EncryptedExtensions", [11] = "Certificate",
        [15] = "CertificateVerify", [20] = "Finished",
    };
    int type = len > 0 ? ((const unsigned char *)buf)[0] : 0;

    (void)version; (void)ssl; (void)arg;
    if (content_type != SSL3_RT_HANDSHAKE)
        return;
    printf("  %8.3f ms  %s %-20s %5zu bytes\n", now_ms() - trace_t0,
           write_p ? "sent    " : "received",
           type < 21 && names[type] ? names[type] : "other", len);
}

static int tcp_connect(void)
{
    struct addrinfo hints, *res = NULL, *ai;
    struct timeval tv = { 10, 0 };
    int fd = -1, err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        perror("connect");
    return fd;
}

/* Find the ML-DSA signature in the response (base64 header or binary body prefix). */
static int extract_signature(const char *resp, size_t resp_len,
                             unsigned char *sig, size_t *sig_len)
{
    char header[64];
    const char *p, *end, *body;
    int n;

    body = strstr(resp, "\r\n\r\n");
    if (body == NULL)
        return 0;
    body += 4;
    if (binary) {
        p = strstr(resp, "PQ-Auth-Signature-Length: ");
        if (p == NULL || p > body)
            return 0;
        *sig_len = strtoul(p + strlen("PQ-Auth-Signature-Length: "), NULL, 10);
        if (*sig_len == 0 || *sig_len > MAX_SIG || body + *sig_len > resp + resp_len)
            return 0;
        memcpy(sig, body, *sig_len);
        return 1;
    }
    snprintf(header, sizeof(header), "PQ-Auth-Signature: %s ", pq_name);
    p = strstr(resp, header);
    if (p == NULL || p > body)
        return 0;
    p += strlen(header);
    end = strstr(p, "\r\n");
    n = end ? (int)(end - p) : 0;
    if (n == 0 || n % 4 != 0 || n / 4 * 3 > MAX_SIG)
        return 0;
    if (p[n - 1] == '=')
        *sig_len = (p[n - 2] == '=') ? 2 : 1;
    else
        *sig_len = 0;
    n = EVP_DecodeBlock(sig, (const unsigned char *)p, n);
    if (n < 0)
        return 0;
    *sig_len = (size_t)n - *sig_len;
    return 1;
}

/* Verify the ML-DSA signature over SIG_PREFIX || exporter. */
static int verify_pq_signature(SSL *ssl, const char *resp, size_t resp_len)
{
    static unsigned char sig[MAX_SIG];
    unsigned char msg[sizeof(SIG_PREFIX) - 1 + EKM_LEN];
    size_t sig_len = 0;
    EVP_MD_CTX *mctx;
    int ok;

    if (!extract_signature(resp, resp_len, sig, &sig_len)) {
        fprintf(stderr, "No valid %s signature in response\n", pq_name);
        return 0;
    }
    memcpy(msg, SIG_PREFIX, sizeof(SIG_PREFIX) - 1);
    if (SSL_export_keying_material(ssl, msg + sizeof(SIG_PREFIX) - 1, EKM_LEN,
                                   EXPORTER_LABEL, strlen(EXPORTER_LABEL),
                                   NULL, 0, 0) != 1)
        return 0;

    mctx = EVP_MD_CTX_new();
    ok = mctx != NULL &&
         EVP_DigestVerifyInit_ex(mctx, NULL, NULL, NULL, NULL, mldsa_pub, NULL) == 1 &&
         EVP_DigestVerify(mctx, sig, sig_len, msg, sizeof(msg)) == 1;
    EVP_MD_CTX_free(mctx);
    if (!ok) {
        fprintf(stderr, "%s signature verification FAILED\n", pq_name);
        ERR_print_errors_fp(stderr);
    }
    return ok;
}

/* One connection. Returns 1 on success with r filled in, 0 on failure. */
static int run_once(SSL_CTX *ctx, int verbose, struct result *r)
{
    static char buf[MAX_RESPONSE + 1];
    char req[256];
    struct byte_count bytes = { 0, 0 };
    double t0, t1, t2, tv;
    size_t total = 0;
    const char *body;
    int fd, n, nid = 0, ok = 0;
    SSL *ssl;

    memset(r, 0, sizeof(*r));
    t0 = now_ms();
    fd = tcp_connect();
    if (fd < 0)
        return 0;
    t1 = now_ms();

    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    BIO_set_callback_ex(SSL_get_rbio(ssl), count_bytes);
    BIO_set_callback_arg(SSL_get_rbio(ssl), (char *)&bytes);
    if (resume && last_session != NULL)
        SSL_set_session(ssl, last_session);
    memset(&v2, 0, sizeof(v2));
    if (certext || getenv("TRACE") != NULL)
        SSL_set_msg_callback(ssl, msg_cb);
    if (getenv("TRACE") != NULL) {
        trace_t0 = now_ms();
        printf("Handshake trace (time since SSL_connect start):\n");
    }

    if (SSL_connect(ssl) != 1) {
        if (parallel)
            v2_join();
        fprintf(stderr, "TLS handshake failed (verify result: %s)\n",
                X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
        ERR_print_errors_fp(stderr);
        goto end;
    }
    /* v2: the handshake is accepted only if the ML-DSA signature was received and verified;
     * in parallel mode wait for the worker here, before any application data is sent. */
    if (certext && !SSL_session_reused(ssl)) {
        tv = now_ms();
        if (!v2.got_sig || !(parallel ? v2_join() : v2.verified)) {
            fprintf(stderr, "v2 ML-DSA-44 signature %s\n", v2.got_sig ? "verification FAILED" : "missing");
            goto end;
        }
        r->wait_ms = now_ms() - tv;
        r->verify_ms = v2.verify_ms;
    }
    t2 = now_ms();
    if (getenv("TRACE") != NULL)
        printf("  %8.3f ms  SSL_connect returned\n", t2 - trace_t0);
    r->hs_rx = bytes.rx;
    r->hs_tx = bytes.tx;
    r->resumed = SSL_session_reused(ssl);

    n = snprintf(req, sizeof(req),
                 "GET / HTTP/1.1\r\nHost: test\r\n%s%s%s%s"
                 "Connection: close\r\n\r\n",
                 hybrid && !certext ? "PQ-Auth: " : "", hybrid && !certext ? pq_name : "",
                 hybrid && !certext ? "\r\n" : "",
                 hybrid && !certext && binary ? "PQ-Auth-Encoding: binary\r\n" : "");
    SSL_write(ssl, req, n);
    while (total < MAX_RESPONSE &&
           (n = SSL_read(ssl, buf + total, (int)(MAX_RESPONSE - total))) > 0)
        total += (size_t)n;
    buf[total] = '\0';

    /* A resumed session inherits the authentication of the full handshake
     * that created its ticket, and only verified sessions are kept. */
    if (!certext) {
        tv = now_ms();
        if (hybrid && !r->resumed && !verify_pq_signature(ssl, buf, total))
            goto end;
        r->verify_ms = now_ms() - tv;
    }
    r->e2e_ms = now_ms() - t0;
    r->tcp_ms = t1 - t0;
    r->handshake_ms = t2 - t1;
    r->rx = bytes.rx;
    r->tx = bytes.tx;
    ok = 1;

    if (resume) {
        SSL_SESSION *s = SSL_get1_session(ssl);

        if (s != NULL && SSL_SESSION_is_resumable(s)) {
            SSL_SESSION_free(last_session);
            last_session = s;
        } else {
            SSL_SESSION_free(s);
        }
    }

    if (verbose) {
        const char *signame = NULL;

        if (r->resumed)
            signame = "none (resumed session)";
        else if (!SSL_get0_peer_signature_name(ssl, &signame) || signame == NULL) {
            SSL_get_peer_signature_type_nid(ssl, &nid);
            signame = nid ? OBJ_nid2sn(nid) : "unknown";
        }
        printf("TLS version:      %s\n", SSL_get_version(ssl));
        printf("Key exchange:     %s\n", SSL_get0_group_name(ssl));
        printf("Server signature: %s (certificate verified: %s)\n", signame,
               r->resumed ? "in original session"
                          : SSL_get_verify_result(ssl) == X509_V_OK ? "yes" : "NO");
        if (certext)
            printf("PQ signature:     %s in TLS Certificate extension over SHA-256(ClientHello||ServerHello),"
                   " verified %s (verified: yes)\n", pq_name,
                   parallel ? "on a worker thread in parallel with ECDSA" : "on the same thread");
        else if (hybrid)
            printf("PQ signature:     %s over TLS exporter, %s (verified: yes)\n",
                   pq_name, binary ? "binary" : "base64");
        else
            printf("PQ signature:     none\n");
        /* The JSON follows the headers (and, in binary mode, the raw signature). */
        body = memmem(buf, total, "{\"key_exchange\"", 15);
        printf("Server response:  %s", body ? body : "(no JSON body)\n");
    }
end:
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    return ok;
}

static double median_of(double *v, int n)
{
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* Median of one field over res[first..ok-1]; leaves the sorted values in v. */
static double field_median(const struct result *res, int first, int ok, double *v,
                           size_t offset, int is_size)
{
    int i, k = 0;

    for (i = first; i < ok; i++) {
        const char *p = (const char *)&res[i] + offset;
        v[k++] = is_size ? (double)*(const size_t *)p : *(const double *)p;
    }
    return median_of(v, k);
}

#define MEDIAN_D(field) field_median(res, first, ok, v, offsetof(struct result, field), 0)
#define MEDIAN_Z(field) field_median(res, first, ok, v, offsetof(struct result, field), 1)

int main(int argc, char **argv)
{
    const char *group, *auth, *dir;
    char path[512];
    struct result *res;
    double *v, e2e, tcp, hs, ver, wait, rx, tx;
    struct rusage ru;
    SSL_CTX *ctx;
    int runs, i, ok = 0, first = 0, n;
    FILE *f;

    if (argc < 5) {
        fprintf(stderr, "Usage: %s host port group rsa|hybrid44|hybrid65 [runs] "
                        "[key_dir] [binary] [resume]\n", argv[0]);
        return 1;
    }
    host = argv[1];
    port = argv[2];
    group = argv[3];
    auth = argv[4];
    runs = argc > 5 ? atoi(argv[5]) : 1;
    dir = argc > 6 ? argv[6] : "keys";
    for (i = 7; i < argc; i++) {
        binary |= strcmp(argv[i], "binary") == 0;
        resume |= strcmp(argv[i], "resume") == 0;
        certext |= strcmp(argv[i], "certext") == 0;
        parallel |= strcmp(argv[i], "par") == 0;
    }
    if (strcmp(auth, "hybrid44") == 0)
        pq_name = "ML-DSA-44";
    else if (strcmp(auth, "hybrid65") == 0 || strcmp(auth, "hybrid") == 0)
        pq_name = "ML-DSA-65";
    else if (strcmp(auth, "rsa") != 0) {
        fprintf(stderr, "Unknown auth mode '%s'\n", auth);
        return 1;
    }
    hybrid = pq_name != NULL;
    if (certext && (pq_name == NULL || strcmp(pq_name, "ML-DSA-44") != 0)) {
        fprintf(stderr, "certext needs auth mode hybrid44\n");
        return 1;
    }
    if (parallel && !certext) {
        fprintf(stderr, "par needs certext\n");
        return 1;
    }
    if (runs < 1)
        runs = 1;
    if (resume && runs < 2)
        runs = 2;

    printf("OpenSSL library:  %s\n", OpenSSL_version(OPENSSL_VERSION));
    printf("Mode:             %s + %s%s%s%s, %d runs\n", group,
           !hybrid ? "legacy auth (RSA-3072)"
           : strcmp(pq_name, "ML-DSA-44") == 0 ? "hybrid auth (ECDSA P-256 + ML-DSA-44)"
                                               : "hybrid auth (ECDSA P-256 + ML-DSA-65)",
           certext ? (parallel ? ", v2: signature in Certificate, ML-DSA verified on a 2nd core in parallel"
                               : ", v2: signature in Certificate, verified on the same core")
                   : binary ? ", binary signature" : "",
           "", resume ? ", session resumption" : "", runs);

    ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    if (SSL_CTX_set1_groups_list(ctx, group) != 1 ||
        SSL_CTX_set1_sigalgs_list(ctx, hybrid ? "ecdsa_secp256r1_sha256"
                                              : "rsa_pss_rsae_sha256") != 1) {
        fprintf(stderr, "Unsupported group or signature algorithm\n");
        return 1;
    }
    if (resume)
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
    if (certext && SSL_CTX_add_custom_ext(ctx, PQ_AUTH_EXT,
                                          SSL_EXT_TLS1_3_ONLY | SSL_EXT_CLIENT_HELLO |
                                          SSL_EXT_TLS1_3_CERTIFICATE,
                                          v2_add, NULL, NULL, v2_parse, NULL) != 1) {
        ERR_print_errors_fp(stderr);
        return 1;
    }
    if (parallel) {
        pthread_t worker;

        if (pthread_create(&worker, NULL, v2_worker, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
        pthread_detach(worker);
    }
    /* Pin the server certificate as the only trust anchor. */
    snprintf(path, sizeof(path), "%s/%s_cert.pem", dir, hybrid ? "ecdsa" : "rsa");
    if (SSL_CTX_load_verify_locations(ctx, path, NULL) != 1) {
        fprintf(stderr, "Cannot load trust anchor %s\n", path);
        return 1;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    X509_VERIFY_PARAM_set_flags(SSL_CTX_get0_param(ctx), X509_V_FLAG_PARTIAL_CHAIN);
    X509_VERIFY_PARAM_set1_ip_asc(SSL_CTX_get0_param(ctx), host);
    if (hybrid) {
        snprintf(path, sizeof(path), "%s/%s_pub.pem", dir,
                 strcmp(pq_name, "ML-DSA-44") == 0 ? "mldsa44" : "mldsa65");
        if ((f = fopen(path, "r")) == NULL ||
            (mldsa_pub = PEM_read_PUBKEY(f, NULL, NULL, NULL)) == NULL) {
            fprintf(stderr, "Cannot load ML-DSA public key %s\n", path);
            return 1;
        }
        fclose(f);
    }

    res = calloc((size_t)runs, sizeof(*res));
    v = calloc((size_t)runs, sizeof(double));
    for (i = 0; i < runs; i++) {
        if (run_once(ctx, i == 0, &res[ok])) {
            /* RAW=1: one line per connection, for external statistics. */
            if (getenv("RAW") != NULL)
                printf("RAW,%d,%.3f,%.3f,%.3f,%d,%.3f,%.3f\n", i + 1, res[ok].tcp_ms,
                       res[ok].handshake_ms, res[ok].e2e_ms, res[ok].resumed,
                       res[ok].verify_ms, res[ok].wait_ms);
            ok++;
        } else if (getenv("RAW") != NULL) {
            printf("RAW,%d,FAILED\n", i + 1);
        }
        if (i + 1 < runs)
            usleep(200000);
    }
    if (ok == 0)
        return 1;

    if (resume) {
        int resumed = 0;

        for (i = 1; i < ok; i++)
            resumed += res[i].resumed;
        printf("--- first (full) connection: end-to-end %.3f ms; "
               "%d of %d later connections resumed ---\n",
               res[0].e2e_ms, resumed, ok - 1);
        first = 1;
    }
    n = ok - first;
    if (n == 0)
        return 1;
    printf("--- %d of %d runs succeeded, medians over %d%s connections ---\n",
           ok, runs, n, resume ? " resumed" : "");
    tcp = MEDIAN_D(tcp_ms);
    hs = MEDIAN_D(handshake_ms);
    ver = MEDIAN_D(verify_ms);
    wait = MEDIAN_D(wait_ms);
    rx = MEDIAN_Z(rx);
    tx = MEDIAN_Z(tx);
    e2e = MEDIAN_D(e2e_ms);     /* last, so v holds the sorted e2e for min/max */
    printf("TCP connect:      %8.3f ms\n", tcp);
    printf("TLS handshake:    %8.3f ms\n", hs);
    printf("PQ verify:        %8.3f ms\n", ver);
    if (certext)
        printf("Wait after TLS:   %8.3f ms   (time spent waiting for the ML-DSA result after OpenSSL finished)\n", wait);
    printf("End-to-end:       %8.3f ms   (min %.3f / max %.3f)\n", e2e, v[0], v[n - 1]);
    printf("Total bytes:      %8.0f rx / %.0f tx\n", rx, tx);
    getrusage(RUSAGE_SELF, &ru);
    printf("Peak memory:      %8ld KB (max RSS)\n", ru.ru_maxrss);
    printf("CSV,%s,%s,%s%s,%d,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%ld\n", group, auth,
           certext ? (parallel ? "certext+par" : "certext") : binary ? "binary" : "base64",
           resume ? "+resume" : "", n,
           tcp, hs, ver, e2e, rx, tx, ru.ru_maxrss);

    free(res);
    free(v);
    SSL_SESSION_free(last_session);
    EVP_PKEY_free(mldsa_pub);
    SSL_CTX_free(ctx);
    return 0;
}
