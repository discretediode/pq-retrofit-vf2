/*
 * pqc_client.c - C version of run_me.py with a selectable key-exchange group.
 *
 * Connects to the handshake-detecting server over HTTPS, prints the TLS
 * version, cipher, key-exchange group, signature type, handshake time and
 * the server's response. Needs OpenSSL 3.5 or newer for the hybrid groups.
 *
 * Usage:   ./pqc_client [group] [runs] [host]
 * Example: ./pqc_client X25519 10            (classical, for comparison)
 *          ./pqc_client X25519MLKEM768 10    (hybrid key exchange)
 *          ./pqc_client SecP256r1MLKEM768 10 (hybrid key exchange, P-256)
 *          ./pqc_client X25519MLKEM768 10 193.30.40.242   (server over IPv4)
 * host is an IPv4 or IPv6 address or a name; default is the server's IPv6 address.
 * PQC_PAUSE_MS in the environment sets the pause between runs (default 1000 ms).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/objects.h>

#define DEFAULT_HOST "2a0f:7cc7:7cc7:7c40::94ad"
#define PORT "443"
#define MAX_RESPONSE 65536

static const char *host = DEFAULT_HOST;
static char request[512];

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

/* Open a TCP connection to the server (IPv4 or IPv6), with a 10 s timeout. */
static int tcp_connect(void)
{
    struct addrinfo hints, *res = NULL;
    struct timeval tv = { 10, 0 };
    int fd, err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    err = getaddrinfo(host, PORT, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        perror(res->ai_family == AF_INET6 ? "connect (does this machine have IPv6?)"
                                          : "connect");
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return fd;
}

static const char *group_name(SSL *ssl)
{
    const char *name;
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
    name = SSL_get0_group_name(ssl);
#else
    name = SSL_group_to_name(ssl, (int)SSL_get_negotiated_group(ssl));
#endif
    return name ? name : "unknown";
}

static const char *peer_signature_name(SSL *ssl)
{
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
    const char *name = NULL;
    if (SSL_get0_peer_signature_name(ssl, &name) && name != NULL)
        return name;
#endif
    {
        int nid = 0;
        if (SSL_get_peer_signature_type_nid(ssl, &nid) && nid != 0)
            return OBJ_nid2sn(nid);
    }
    return "unknown";
}

/* One connection. Returns the handshake time in ms, or -1 on failure. */
static double run_once(SSL_CTX *ctx, int verbose)
{
    static char buf[MAX_RESPONSE + 1];
    size_t total = 0;
    double t0, handshake_ms;
    const char *body;
    SSL *ssl;
    int fd, n;

    fd = tcp_connect();
    if (fd < 0)
        return -1;

    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);

    /* Same span as run_me.py: only the TLS handshake, not the TCP connect. */
    t0 = now_ms();
    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "TLS handshake failed:\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(fd);
        return -1;
    }
    handshake_ms = now_ms() - t0;

    if (verbose) {
        printf("TLS version:      %s\n", SSL_get_version(ssl));
        printf("Cipher:           %s\n", SSL_get_cipher_name(ssl));
        printf("Key exchange:     %s\n", group_name(ssl));
        printf("Server signature: %s\n", peer_signature_name(ssl));
    }
    printf("Handshake took:   %.3f ms\n", handshake_ms);

    SSL_write(ssl, request, (int)strlen(request));
    while (total < MAX_RESPONSE &&
           (n = SSL_read(ssl, buf + total, (int)(MAX_RESPONSE - total))) > 0)
        total += (size_t)n;
    buf[total] = '\0';

    if (verbose) {
        body = strstr(buf, "\r\n\r\n");
        body = body ? body + 4 : buf;
        printf("--- server response ---\n%s", body);
        if (total == 0 || buf[total - 1] != '\n')
            printf("\n");
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    return handshake_ms;
}

int main(int argc, char **argv)
{
    const char *groups = argc > 1 ? argv[1] : "X25519MLKEM768";
    int runs = argc > 2 ? atoi(argv[2]) : 1;
    double *times;
    int i, ok = 0, pause_ms = 1000;
    SSL_CTX *ctx;

    if (runs < 1)
        runs = 1;
    if (argc > 3)
        host = argv[3];
    /* Pause between runs; PQC_PAUSE_MS overrides the default 1 s. */
    if (getenv("PQC_PAUSE_MS") != NULL)
        pause_ms = atoi(getenv("PQC_PAUSE_MS"));

    /* IPv6 literals need brackets in the Host header. */
    snprintf(request, sizeof(request),
             "GET /run_me.py HTTP/1.1\r\n"
             "Host: %s%s%s\r\n"
             "User-Agent: pqc_client/1.0\r\n"
             "Connection: close\r\n"
             "\r\n",
             strchr(host, ':') ? "[" : "", host, strchr(host, ':') ? "]" : "");

    printf("OpenSSL library:  %s\n", OpenSSL_version(OPENSSL_VERSION));
    printf("Server:           %s\n", host);
    printf("Requested group:  %s\n", groups);

    ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL) {
        ERR_print_errors_fp(stderr);
        return 1;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);

    /* Self-signed certificate on the server, same as run_me.py:
     * observe the handshake, do not authenticate the peer. */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    if (SSL_CTX_set1_groups_list(ctx, groups) != 1) {
        fprintf(stderr,
                "Group '%s' is not supported by this OpenSSL build.\n"
                "Hybrid groups need OpenSSL 3.5 or newer.\n", groups);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return 1;
    }

    times = malloc(sizeof(double) * (size_t)runs);
    if (times == NULL)
        return 1;

    for (i = 0; i < runs; i++) {
        double ms = run_once(ctx, i == 0);
        if (ms >= 0)
            times[ok++] = ms;
        if (i + 1 < runs)
            usleep(pause_ms * 1000);
    }

    if (ok > 1) {
        qsort(times, (size_t)ok, sizeof(double), cmp_double);
        printf("--- summary (%d of %d runs succeeded) ---\n", ok, runs);
        printf("min / median / max: %.3f / %.3f / %.3f ms\n",
               times[0], times[ok / 2], times[ok - 1]);
    }

    free(times);
    SSL_CTX_free(ctx);
    return ok > 0 ? 0 : 1;
}
