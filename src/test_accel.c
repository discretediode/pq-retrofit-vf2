/* test_accel <keydir> [runs]
 * 1. Exactness: accelerator output == CPU output for many random challenges.
 * 2. Timing: setup cost, accelerator per call, and the normal CPU (NTT) path.
 * 3. End-to-end: sign + verify with the accelerated ML-DSA must succeed,
 *    and a corrupted signature must be rejected. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params.h"
#include "sign.h"
#include "packing.h"
#include "poly.h"
#include "polyvec.h"
#include "randombytes.h"
#include "accel.h"

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

static double median(double *v, int n) { qsort(v, n, sizeof *v, cmp); return v[n / 2]; }

static void rd(const char *dir, const char *name, uint8_t *buf, size_t len)
{
  char path[1024];
  FILE *f;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  f = fopen(path, "rb");
  if(!f || fread(buf, 1, len, f) != len) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
  fclose(f);
}

static void random_challenge(poly *c)
{
  uint8_t seed[CTILDEBYTES];
  randombytes(seed, sizeof seed);
  poly_challenge(c, seed);
}

int main(int argc, char **argv)
{
  static uint8_t pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES], sig[CRYPTO_BYTES];
  uint8_t rho[SEEDBYTES], tr[TRBYTES], key[SEEDBYTES], msg[32];
  polyvecl s1, s1hat, a1, b1;
  polyveck s2, s2hat, t0, t1, a2, b2;
  poly c, chat;
  int runs = argc > 2 ? atoi(argv[2]) : 1000;
  double *t = malloc(sizeof(double) * runs), t0us;
  long bad = 0;
  size_t siglen;
  int i, j, r, signing;

  if(argc < 2 || runs < 1) { fprintf(stderr, "usage: test_accel <keydir> [runs]\n"); return 1; }
  rd(argv[1], "mldsa_pk.bin", pk, sizeof pk);
  rd(argv[1], "mldsa_sk.bin", sk, sizeof sk);
  unpack_sk(rho, tr, key, &t0, &s1, &s2, sk);
  unpack_pk(rho, &t1, pk);

  t0us = now_us();
  accel_init();
  printf("backend: %s\n", accel_backend());
  printf("setup_us: %.1f\n", now_us() - t0us);
  signing = strcmp(accel_backend(), "hailo-verify") != 0;

  /* 1. exactness + accelerator timing */
  for(r = 0; r < runs; r++) {
    random_challenge(&c);
    if(signing) {
      t0us = now_us();
      accel_cs(&a1, &a2, &c, &s1, &s2);
      t[r] = now_us() - t0us;
      accel_cs_cpu(&b1, &b2, &c, &s1, &s2);
      for(i = 0; i < L; i++) for(j = 0; j < N; j++) bad += a1.vec[i].coeffs[j] != b1.vec[i].coeffs[j];
      for(i = 0; i < K; i++) for(j = 0; j < N; j++) bad += a2.vec[i].coeffs[j] != b2.vec[i].coeffs[j];
    } else {
      t0us = now_us();
      accel_ct1(&a2, &c, &t1);
      t[r] = now_us() - t0us;
      accel_ct1_cpu(&b2, &c, &t1);
      for(i = 0; i < K; i++) for(j = 0; j < N; j++) bad += a2.vec[i].coeffs[j] != b2.vec[i].coeffs[j];
    }
  }
  printf("op: %s\n", signing ? "c*s1,c*s2 (signing)" : "c*t1 (verification)");
  printf("wrong_values: %ld of %ld  %s\n", bad, (long)runs * 2048,
         bad ? "<-- NOT EXACT" : "(exact)");
  printf("accel_call_median_us: %.2f\n", median(t, runs));

  /* 2. what the CPU normally does instead: NTT path (key NTT done once, like sign.c) */
  s1hat = s1; s2hat = s2;
  polyvecl_ntt(&s1hat); polyveck_ntt(&s2hat);
  for(r = 0; r < runs; r++) {
    random_challenge(&c);
    t0us = now_us();
    chat = c;
    poly_ntt(&chat);
    if(signing) {
      polyvecl_pointwise_poly_montgomery(&a1, &chat, &s1hat); polyvecl_invntt_tomont(&a1);
      polyveck_pointwise_poly_montgomery(&a2, &chat, &s2hat); polyveck_invntt_tomont(&a2);
    } else {
      b2 = t1; polyveck_shiftl(&b2); polyveck_ntt(&b2);
      polyveck_pointwise_poly_montgomery(&a2, &chat, &b2); polyveck_invntt_tomont(&a2);
    }
    t[r] = now_us() - t0us;
  }
  printf("cpu_ntt_path_median_us: %.2f\n", median(t, runs));

  /* 3. end-to-end sign/verify through the patched ML-DSA */
  for(r = 0; r < 20; r++) {
    randombytes(msg, sizeof msg);
    if(crypto_sign_signature(sig, &siglen, msg, sizeof msg, NULL, 0, sk)) { puts("sign FAILED"); return 1; }
    if(crypto_sign_verify(sig, siglen, msg, sizeof msg, NULL, 0, pk)) { puts("verify FAILED"); return 1; }
    sig[r * 7 % CRYPTO_BYTES] ^= 1;
    if(!crypto_sign_verify(sig, siglen, msg, sizeof msg, NULL, 0, pk)) { puts("corrupted signature ACCEPTED"); return 1; }
  }
  puts("sign/verify roundtrip: OK (20 signatures, corrupted ones rejected)");
  free(t);
  return bad ? 1 : 0;
}
