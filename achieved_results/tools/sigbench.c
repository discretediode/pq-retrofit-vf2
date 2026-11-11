/*
 * sigbench.c - time one signature verification the way the TLS client does it:
 * public key from the real certificate / key file, EVP_DigestVerify, N repeats.
 *
 * Usage: ./sigbench [repeats] [key_dir]   (needs the *_key.pem files only to sign
 *        the test message; run it where those exist, or pass pre-made sigs)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void bench(const char *name, const char *md, int pss, int reps)
{
    static const unsigned char msg[] = "TLS 1.3, server CertificateVerify";
    unsigned char sig[8192];
    size_t sig_len = sizeof(sig);
    EVP_PKEY *key = NULL;
    EVP_PKEY_CTX *kctx;
    EVP_MD_CTX *mctx;
    double t0, sign_ms, verify_ms;
    int i, ok = 1;

    if (strcmp(name, "RSA-3072") == 0) {
        key = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", (size_t)3072);
    } else if (strcmp(name, "ECDSA-P256") == 0) {
        key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    } else {
        key = EVP_PKEY_Q_keygen(NULL, NULL, name);
    }
    if (key == NULL) {
        ERR_print_errors_fp(stderr);
        return;
    }

    mctx = EVP_MD_CTX_new();
    t0 = now_ms();
    for (i = 0; i < reps && ok; i++) {
        sig_len = sizeof(sig);
        ok = EVP_DigestSignInit_ex(mctx, &kctx, md, NULL, NULL, key, NULL) == 1 &&
             (!pss || EVP_PKEY_CTX_set_rsa_padding(kctx, RSA_PKCS1_PSS_PADDING) == 1) &&
             EVP_DigestSign(mctx, sig, &sig_len, msg, sizeof(msg)) == 1;
    }
    sign_ms = (now_ms() - t0) / reps;

    t0 = now_ms();
    for (i = 0; i < reps && ok; i++)
        ok = EVP_DigestVerifyInit_ex(mctx, &kctx, md, NULL, NULL, key, NULL) == 1 &&
             (!pss || EVP_PKEY_CTX_set_rsa_padding(kctx, RSA_PKCS1_PSS_PADDING) == 1) &&
             EVP_DigestVerify(mctx, sig, sig_len, msg, sizeof(msg)) == 1;
    verify_ms = (now_ms() - t0) / reps;

    if (!ok)
        ERR_print_errors_fp(stderr);
    printf("%-11s sign %8.3f ms   verify %8.3f ms   signature %5zu bytes%s\n",
           name, sign_ms, verify_ms, sig_len, ok ? "" : "   (FAILED)");
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(key);
}

int main(int argc, char **argv)
{
    int reps = argc > 1 ? atoi(argv[1]) : 100;

    printf("OpenSSL %s, %d repeats each\n", OpenSSL_version(OPENSSL_VERSION_STRING), reps);
    bench("RSA-3072", "SHA256", 1, reps);
    bench("ECDSA-P256", "SHA256", 0, reps);
    bench("ML-DSA-44", NULL, 0, reps);
    bench("ML-DSA-65", NULL, 0, reps);
    return 0;
}
