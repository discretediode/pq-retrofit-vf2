/* keytool gen <dir>                  -> <dir>/mldsa_pk.bin, <dir>/mldsa_sk.bin
 * keytool sign-weights <sk> <out>    -> int8  s1||s2, (L+K)*N values in [-2,2]
 * keytool verify-weights <pk> <out>  -> int16 t1,     K*N values in [0,1023]
 * The weights files feed hailo/make_model.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "sign.h"
#include "packing.h"

static void die(const char *m) { fprintf(stderr, "keytool: %s\n", m); exit(1); }

static void rd(const char *path, uint8_t *buf, size_t len)
{
  FILE *f = fopen(path, "rb");
  if(!f || fread(buf, 1, len, f) != len) die("cannot read key file (wrong size?)");
  fclose(f);
}

static void wr(const char *path, const void *buf, size_t len)
{
  FILE *f = fopen(path, "wb");
  if(!f || fwrite(buf, 1, len, f) != len) die("cannot write file");
  fclose(f);
}

int main(int argc, char **argv)
{
  static uint8_t pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES];
  uint8_t rho[SEEDBYTES], tr[TRBYTES], key[SEEDBYTES];
  polyvecl s1;
  polyveck s2, t0, t1;
  char path[1024];
  int i, j;

  if(argc == 3 && !strcmp(argv[1], "gen")) {
    crypto_sign_keypair(pk, sk);
    snprintf(path, sizeof path, "%s/mldsa_pk.bin", argv[2]); wr(path, pk, sizeof pk);
    snprintf(path, sizeof path, "%s/mldsa_sk.bin", argv[2]); wr(path, sk, sizeof sk);
    printf("wrote ML-DSA-44 keys to %s/\n", argv[2]);
  } else if(argc == 4 && !strcmp(argv[1], "sign-weights")) {
    int8_t w[(L + K) * N];
    rd(argv[2], sk, sizeof sk);
    unpack_sk(rho, tr, key, &t0, &s1, &s2, sk);
    for(i = 0; i < L; i++) for(j = 0; j < N; j++) w[i * N + j] = (int8_t)s1.vec[i].coeffs[j];
    for(i = 0; i < K; i++) for(j = 0; j < N; j++) w[(L + i) * N + j] = (int8_t)s2.vec[i].coeffs[j];
    wr(argv[3], w, sizeof w);
    printf("wrote %d int8 weights to %s\n", (L + K) * N, argv[3]);
  } else if(argc == 4 && !strcmp(argv[1], "verify-weights")) {
    int16_t w[K * N];
    rd(argv[2], pk, sizeof pk);
    unpack_pk(rho, &t1, pk);
    for(i = 0; i < K; i++) for(j = 0; j < N; j++) w[i * N + j] = (int16_t)t1.vec[i].coeffs[j];
    wr(argv[3], w, sizeof w);
    printf("wrote %d int16 weights to %s\n", K * N, argv[3]);
  } else {
    fprintf(stderr, "usage: keytool gen <dir> | sign-weights <sk> <out> | verify-weights <pk> <out>\n");
    return 1;
  }
  return 0;
}
