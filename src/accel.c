#include "accel.h"

/* out = c*a in Z[x]/(x^N + 1). Schoolbook, skipping the zero entries of c
 * (only TAU=39 of 256 are non-zero). c is public, so the data-dependent
 * skip leaks nothing. */
static void negacyclic_mul(int32_t out[N], const int32_t c[N], const int32_t a[N])
{
  int i, j;
  for(i = 0; i < N; i++)
    out[i] = 0;
  for(i = 0; i < N; i++) {
    if(!c[i])
      continue;
    for(j = 0; j < N; j++) {
      if(i + j < N) out[i + j] += c[i] * a[j];
      else          out[i + j - N] -= c[i] * a[j];
    }
  }
}

void accel_cs_cpu(polyvecl *cs1, polyveck *cs2, const poly *c,
                  const polyvecl *s1, const polyveck *s2)
{
  int i;
  for(i = 0; i < L; i++)
    negacyclic_mul(cs1->vec[i].coeffs, c->coeffs, s1->vec[i].coeffs);
  for(i = 0; i < K; i++)
    negacyclic_mul(cs2->vec[i].coeffs, c->coeffs, s2->vec[i].coeffs);
}

void accel_ct1_cpu(polyveck *ct1, const poly *c, const polyveck *t1)
{
  int i;
  for(i = 0; i < K; i++)
    negacyclic_mul(ct1->vec[i].coeffs, c->coeffs, t1->vec[i].coeffs);
}

#ifdef USE_HAILO

void accel_init(void) { hailo_open(); }

const char *accel_backend(void)
{
  switch(hailo_open()) {
    case 1:  return "hailo-sign";
    case 2:  return "hailo-verify";
    default: return "cpu";
  }
}

/* The Hailo model has the key baked in, so s1/s2/t1 are ignored there.
 * A model built from a different key gives wrong results -> test_accel catches it. */
void accel_cs(polyvecl *cs1, polyveck *cs2, const poly *c,
              const polyvecl *s1, const polyveck *s2)
{
  if(hailo_open() == 1) hailo_cs(cs1, cs2, c);
  else                  accel_cs_cpu(cs1, cs2, c, s1, s2);
}

void accel_ct1(polyveck *ct1, const poly *c, const polyveck *t1)
{
  if(hailo_open() == 2) hailo_ct1(ct1, c);
  else                  accel_ct1_cpu(ct1, c, t1);
}

#else

void accel_init(void) {}
const char *accel_backend(void) { return "cpu"; }

void accel_cs(polyvecl *cs1, polyveck *cs2, const poly *c,
              const polyvecl *s1, const polyveck *s2)
{
  accel_cs_cpu(cs1, cs2, c, s1, s2);
}

void accel_ct1(polyveck *ct1, const poly *c, const polyveck *t1)
{
  accel_ct1_cpu(ct1, c, t1);
}

#endif
