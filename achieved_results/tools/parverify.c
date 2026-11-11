/*
 * parverify.c - can the classical and post-quantum halves of a hybrid handshake
 * run faster on two CPU cores than one after another?
 *
 * Pairs (the client-side work of one hybrid TLS handshake):
 *   auth        ECDSA P-256 verify        ||  ML-DSA-44 verify
 *   kex_keygen  X25519 keygen             ||  ML-KEM-768 keygen
 *   kex_decaps  X25519 shared secret      ||  ML-KEM-768 decapsulation
 * Modes:
 *   seq         both halves one after another on the calling thread
 *   par_create  classical half on a new thread (pthread_create/join per operation)
 *   par_pool    classical half on a persistent worker thread, woken per operation
 * The modes rotate every iteration, with a pause between iterations so the CPU idles
 * like between real handshakes. Output: CSV lines "RAW,pair,mode,iter,us,cpu_a,cpu_b".
 *
 * Usage: ./parverify [iterations] [pause_ms] [label]   (defaults 200, 100, "-")
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

typedef int (*task_fn)(void *);

struct task {
    task_fn fn;
    void *arg;
    int ok, cpu;
};

/* ---- the six operations ------------------------------------------------- */

static const unsigned char msg[] = "TLS 1.3 server CertificateVerify / PQ exporter signature";
static EVP_PKEY *ecdsa_key, *mldsa_key, *x25519_mine, *x25519_peer, *mlkem_key;
static unsigned char ecdsa_sig[256], mldsa_sig[4096], mlkem_ct[2048];
static size_t ecdsa_sig_len, mldsa_sig_len, mlkem_ct_len;

static int verify(EVP_PKEY *key, const char *md, const unsigned char *sig, size_t len)
{
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    int ok = c && EVP_DigestVerifyInit_ex(c, NULL, md, NULL, NULL, key, NULL) == 1 &&
             EVP_DigestVerify(c, sig, len, msg, sizeof(msg)) == 1;
    EVP_MD_CTX_free(c);
    return ok;
}

static int op_ecdsa_verify(void *a) { (void)a; return verify(ecdsa_key, "SHA256", ecdsa_sig, ecdsa_sig_len); }
static int op_mldsa_verify(void *a) { (void)a; return verify(mldsa_key, NULL, mldsa_sig, mldsa_sig_len); }

static int op_keygen(void *name)
{
    EVP_PKEY *k = EVP_PKEY_Q_keygen(NULL, NULL, (const char *)name);
    EVP_PKEY_free(k);
    return k != NULL;
}

static int op_x25519_derive(void *a)
{
    unsigned char secret[64];
    size_t len = sizeof(secret);
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(NULL, x25519_mine, NULL);
    int ok = c && EVP_PKEY_derive_init(c) == 1 && EVP_PKEY_derive_set_peer(c, x25519_peer) == 1 &&
             EVP_PKEY_derive(c, secret, &len) == 1;

    (void)a;
    EVP_PKEY_CTX_free(c);
    return ok;
}

static int op_mlkem_decaps(void *a)
{
    unsigned char secret[64];
    size_t len = sizeof(secret);
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(NULL, mlkem_key, NULL);
    int ok = c && EVP_PKEY_decapsulate_init(c, NULL) == 1 &&
             EVP_PKEY_decapsulate(c, secret, &len, mlkem_ct, mlkem_ct_len) == 1;

    (void)a;
    EVP_PKEY_CTX_free(c);
    return ok;
}

static int setup(void)
{
    EVP_MD_CTX *c;
    EVP_PKEY_CTX *k;
    unsigned char secret[64];
    size_t slen = sizeof(secret);
    int ok;

    ecdsa_key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    mldsa_key = EVP_PKEY_Q_keygen(NULL, NULL, "ML-DSA-44");
    x25519_mine = EVP_PKEY_Q_keygen(NULL, NULL, "X25519");
    x25519_peer = EVP_PKEY_Q_keygen(NULL, NULL, "X25519");
    mlkem_key = EVP_PKEY_Q_keygen(NULL, NULL, "ML-KEM-768");
    if (!ecdsa_key || !mldsa_key || !x25519_mine || !x25519_peer || !mlkem_key)
        return 0;

    ecdsa_sig_len = sizeof(ecdsa_sig);
    mldsa_sig_len = sizeof(mldsa_sig);
    /* A separate context per signature: re-initialising one context for a second
     * key type carries settings over and produced an invalid ML-DSA signature. */
    c = EVP_MD_CTX_new();
    ok = EVP_DigestSignInit_ex(c, NULL, "SHA256", NULL, NULL, ecdsa_key, NULL) == 1 &&
         EVP_DigestSign(c, ecdsa_sig, &ecdsa_sig_len, msg, sizeof(msg)) == 1;
    EVP_MD_CTX_free(c);
    c = EVP_MD_CTX_new();
    ok = ok && EVP_DigestSignInit_ex(c, NULL, NULL, NULL, NULL, mldsa_key, NULL) == 1 &&
         EVP_DigestSign(c, mldsa_sig, &mldsa_sig_len, msg, sizeof(msg)) == 1;
    EVP_MD_CTX_free(c);
    /* Self-check: every operation must succeed before anything is timed. */
    ok = ok && verify(ecdsa_key, "SHA256", ecdsa_sig, ecdsa_sig_len) &&
         verify(mldsa_key, NULL, mldsa_sig, mldsa_sig_len);

    mlkem_ct_len = sizeof(mlkem_ct);
    k = EVP_PKEY_CTX_new_from_pkey(NULL, mlkem_key, NULL);
    ok = ok && k && EVP_PKEY_encapsulate_init(k, NULL) == 1 &&
         EVP_PKEY_encapsulate(k, mlkem_ct, &mlkem_ct_len, secret, &slen) == 1;
    EVP_PKEY_CTX_free(k);
    return ok;
}

/* ---- execution modes ---------------------------------------------------- */

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void run_task(struct task *t)
{
    t->ok = t->fn(t->arg);
#ifdef __linux__
    t->cpu = sched_getcpu();     /* which core ran it, to confirm two cores were used */
#else
    t->cpu = -1;
#endif
}

static void *thread_main(void *p)
{
    run_task((struct task *)p);
    return NULL;
}

/* Persistent worker: waits for a task, runs it, signals completion. */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_start = PTHREAD_COND_INITIALIZER, cv_done = PTHREAD_COND_INITIALIZER;
static struct task *pool_task;
static int pool_busy;

static void *pool_main(void *p)
{
    (void)p;
    pthread_mutex_lock(&mu);
    for (;;) {
        while (!pool_busy)
            pthread_cond_wait(&cv_start, &mu);
        pthread_mutex_unlock(&mu);
        run_task(pool_task);
        pthread_mutex_lock(&mu);
        pool_busy = 0;
        pthread_cond_signal(&cv_done);
    }
    return NULL;
}

enum mode { SEQ, PAR_CREATE, PAR_POOL, NMODES };
static const char *mode_names[] = { "seq", "par_create", "par_pool" };

static int run_pair(enum mode m, struct task *a, struct task *b)
{
    pthread_t th;

    switch (m) {
    case SEQ:
        run_task(a);
        run_task(b);
        break;
    case PAR_CREATE:
        if (pthread_create(&th, NULL, thread_main, a) != 0)
            return 0;
        run_task(b);
        pthread_join(th, NULL);
        break;
    case PAR_POOL:
        pthread_mutex_lock(&mu);
        pool_task = a;
        pool_busy = 1;
        pthread_cond_signal(&cv_start);
        pthread_mutex_unlock(&mu);
        run_task(b);
        pthread_mutex_lock(&mu);
        while (pool_busy)
            pthread_cond_wait(&cv_done, &mu);
        pthread_mutex_unlock(&mu);
        break;
    default:
        return 0;
    }
    return a->ok && b->ok;
}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 200;
    int pause_ms = argc > 2 ? atoi(argv[2]) : 100;
    const char *label = argc > 3 ? argv[3] : "-";
    struct {
        const char *name;
        struct task a, b;     /* a = classical (other core), b = post-quantum (this core) */
    } pairs[] = {
        { "auth",       { op_ecdsa_verify, NULL, 0, 0 },      { op_mldsa_verify, NULL, 0, 0 } },
        { "kex_keygen", { op_keygen, "X25519", 0, 0 },        { op_keygen, "ML-KEM-768", 0, 0 } },
        { "kex_decaps", { op_x25519_derive, NULL, 0, 0 },     { op_mlkem_decaps, NULL, 0, 0 } },
    };
    pthread_t pool;
    int i, p, k, failures = 0;

    if (!setup()) {
        ERR_print_errors_fp(stderr);
        return 1;
    }
    pthread_create(&pool, NULL, pool_main, NULL);
    printf("# parverify: OpenSSL %s, %ld online CPUs, %d iterations, pause %d ms, label %s\n",
           OpenSSL_version(OPENSSL_VERSION_STRING), sysconf(_SC_NPROCESSORS_ONLN), iters, pause_ms, label);

    /* Warm-up: first use of each algorithm loads/initialises provider code. */
    for (p = 0; p < 3; p++)
        for (k = 0; k < NMODES; k++)
            run_pair((enum mode)k, &pairs[p].a, &pairs[p].b);

    for (i = 0; i < iters; i++) {
        for (p = 0; p < 3; p++) {
            for (k = 0; k < NMODES; k++) {
                enum mode m = (enum mode)((k + i) % NMODES);   /* rotate the order */
                double t0;
                int ok;

                usleep(pause_ms * 1000);
                t0 = now_us();
                ok = run_pair(m, &pairs[p].a, &pairs[p].b);
                printf("RAW,%s,%s,%s,%d,%.1f,%d,%d\n", label, pairs[p].name, mode_names[m], i + 1,
                       now_us() - t0, pairs[p].a.cpu, pairs[p].b.cpu);
                if (!ok)
                    failures++;
            }
        }
    }
    printf("# failures: %d\n", failures);
    return failures != 0;
}
