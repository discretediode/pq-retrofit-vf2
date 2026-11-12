/* bench <keydir> [runs]
 * Times RSA-2048, ECDSA P-256, ML-DSA-44 and hybrid ECDSA+ML-DSA, with the
 * hybrid run both one-after-the-other (seq) and on two cores at once (par).
 * Prints CSV: op,backend,median_us,min_us,max_us,sig_bytes */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "params.h"
#include "sign.h"
#include "randombytes.h"
#ifdef ACCEL
#include "accel.h"
#define BACKEND accel_backend()
#else
#define BACKEND "ref"
#endif

static uint8_t pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES];
static uint8_t msg[32], hash[32];
static uint8_t pq_sig[CRYPTO_BYTES], rsa_sig[256], ec_sig[MBEDTLS_ECDSA_MAX_LEN];
static size_t pq_len, ec_len;
static mbedtls_rsa_context rsa;
static mbedtls_ecdsa_context ec;
static mbedtls_ctr_drbg_context drbg_main, drbg_ec; /* one per thread */

static void die(const char *m) { fprintf(stderr, "bench: %s\n", m); exit(1); }

static double now_us(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int cmp(const void *a, const void *b)
{
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

/* ---- the individual operations; each returns 0 on success ---- */
static int rsa_sign(void)   { return mbedtls_rsa_pkcs1_sign(&rsa, mbedtls_ctr_drbg_random, &drbg_main, MBEDTLS_MD_SHA256, 32, hash, rsa_sig); }
static int rsa_verify(void) { return mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32, hash, rsa_sig); }
static int ec_sign(void)    { return mbedtls_ecdsa_write_signature(&ec, MBEDTLS_MD_SHA256, hash, 32, ec_sig, sizeof ec_sig, &ec_len, mbedtls_ctr_drbg_random, &drbg_ec); }
static int ec_verify(void)  { return mbedtls_ecdsa_read_signature(&ec, hash, 32, ec_sig, ec_len); }
static int pq_sign(void)    { return crypto_sign_signature(pq_sig, &pq_len, msg, sizeof msg, NULL, 0, sk); }
static int pq_verify(void)  { return crypto_sign_verify(pq_sig, pq_len, msg, sizeof msg, NULL, 0, pk); }

/* Hybrid = both signatures. Verification passes only if BOTH pass. */
static int hyb_sign_seq(void)   { return ec_sign() | pq_sign(); }
static int hyb_verify_seq(void) { return ec_verify() | pq_verify(); }

static void *ec_sign_thread(void *r)   { *(int *)r = ec_sign();   return NULL; }
static void *ec_verify_thread(void *r) { *(int *)r = ec_verify(); return NULL; }

/* ECDSA on a second core while ML-DSA runs on this one.
 * ponytail: one thread created per call; that overhead is part of the
 * measurement. If it shows up, switch to a persistent worker thread. */
static int run_par(void *(*ec_fn)(void *), int (*pq_fn)(void))
{
  pthread_t th;
  int ec_ret = -1, pq_ret;
  if(pthread_create(&th, NULL, ec_fn, &ec_ret)) die("pthread_create");
  pq_ret = pq_fn();
  pthread_join(th, NULL);
  return ec_ret | pq_ret;
}
static int hyb_sign_par(void)   { return run_par(ec_sign_thread, pq_sign); }
static int hyb_verify_par(void) { return run_par(ec_verify_thread, pq_verify); }

static void bench(const char *name, int (*fn)(void), int runs, size_t sig_bytes)
{
  double *t = malloc(sizeof(double) * runs), s;
  int i;
  if(fn()) { fprintf(stderr, "%s failed\n", name); exit(1); } /* warm-up + check */
  for(i = 0; i < runs; i++) {
    s = now_us();
    if(fn()) { fprintf(stderr, "%s failed\n", name); exit(1); }
    t[i] = now_us() - s;
  }
  qsort(t, runs, sizeof *t, cmp);
  printf("%s,%s,%.1f,%.1f,%.1f,%zu\n", name, BACKEND, t[runs / 2], t[0], t[runs - 1], sig_bytes);
  fflush(stdout);
  free(t);
}

static void rd(const char *dir, const char *name, uint8_t *buf, size_t len)
{
  char path[1024];
  FILE *f;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  f = fopen(path, "rb");
  if(!f || fread(buf, 1, len, f) != len) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
  fclose(f);
}

int main(int argc, char **argv)
{
  mbedtls_entropy_context ent;
  int runs = argc > 2 ? atoi(argv[2]) : 200;
  int rsa_runs = runs < 20 ? runs : runs / 10; /* RSA signing is slow */

  if(argc < 2 || runs < 1) die("usage: bench <keydir> [runs]");
  rd(argv[1], "mldsa_pk.bin", pk, sizeof pk);
  rd(argv[1], "mldsa_sk.bin", sk, sizeof sk);
#ifdef ACCEL
  accel_init();
#endif

  mbedtls_entropy_init(&ent);
  mbedtls_ctr_drbg_init(&drbg_main);
  mbedtls_ctr_drbg_init(&drbg_ec);
  if(mbedtls_ctr_drbg_seed(&drbg_main, mbedtls_entropy_func, &ent, (const uint8_t *)"main", 4) ||
     mbedtls_ctr_drbg_seed(&drbg_ec, mbedtls_entropy_func, &ent, (const uint8_t *)"ec", 2))
    die("drbg seed");

  mbedtls_rsa_init(&rsa);
  if(mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256) ||
     mbedtls_rsa_gen_key(&rsa, mbedtls_ctr_drbg_random, &drbg_main, 2048, 65537))
    die("rsa keygen");
  mbedtls_ecdsa_init(&ec);
  if(mbedtls_ecdsa_genkey(&ec, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &drbg_main))
    die("ecdsa keygen");

  /* message = stand-in for the handshake transcript */
  randombytes(msg, sizeof msg);
  mbedtls_sha256(msg, sizeof msg, hash, 0);

  /* correctness: a corrupted signature must be rejected by the hybrid check */
  if(ec_sign() || pq_sign() || hyb_verify_par()) die("hybrid self-check failed");
  pq_sig[100] ^= 1;
  if(!hyb_verify_par()) die("corrupted ML-DSA signature was ACCEPTED");
  pq_sig[100] ^= 1;
  ec_sig[ec_len - 1] ^= 1;
  if(!hyb_verify_par()) die("corrupted ECDSA signature was ACCEPTED");
  ec_sig[ec_len - 1] ^= 1;

  puts("op,backend,median_us,min_us,max_us,sig_bytes");
  bench("rsa2048_sign", rsa_sign, rsa_runs, sizeof rsa_sig);
  bench("rsa2048_verify", rsa_verify, runs, sizeof rsa_sig);
  bench("ecdsa_p256_sign", ec_sign, runs, ec_len);
  bench("ecdsa_p256_verify", ec_verify, runs, ec_len);
  bench("mldsa44_sign", pq_sign, runs, CRYPTO_BYTES);
  bench("mldsa44_verify", pq_verify, runs, CRYPTO_BYTES);
  bench("hybrid_sign_seq", hyb_sign_seq, runs, ec_len + CRYPTO_BYTES);
  bench("hybrid_sign_par", hyb_sign_par, runs, ec_len + CRYPTO_BYTES);
  bench("hybrid_verify_seq", hyb_verify_seq, runs, ec_len + CRYPTO_BYTES);
  bench("hybrid_verify_par", hyb_verify_par, runs, ec_len + CRYPTO_BYTES);
  return 0;
}
