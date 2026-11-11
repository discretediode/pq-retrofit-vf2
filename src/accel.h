#ifndef ACCEL_H
#define ACCEL_H

/* The two ML-DSA multiplications we move off the NTT path so an accelerator
 * can do them. Inputs are small, so results are exact integers (no mod q):
 *   signing:      c*s1, c*s2   (c in {-1,0,1}, s in [-2,2])  -> |coef| <= 78
 *   verification: c*t1         (t1 in [0,1023])              -> |coef| <= 39897
 * Backend: CPU by default. Built with USE_HAILO, it uses the Hailo when the
 * env var ACCEL_SIGN_HEF or ACCEL_VERIFY_HEF points at a compiled model. */

#include "params.h"
#include "poly.h"
#include "polyvec.h"

void accel_init(void);           /* optional: pay the setup cost up front */
const char *accel_backend(void); /* "cpu", "hailo-sign" or "hailo-verify" */

void accel_cs(polyvecl *cs1, polyveck *cs2, const poly *c,
              const polyvecl *s1, const polyveck *s2);
void accel_ct1(polyveck *ct1, const poly *c, const polyveck *t1);

/* CPU versions, always available (used as the reference in tests). */
void accel_cs_cpu(polyvecl *cs1, polyveck *cs2, const poly *c,
                  const polyvecl *s1, const polyveck *s2);
void accel_ct1_cpu(polyveck *ct1, const poly *c, const polyveck *t1);

#ifdef USE_HAILO
/* accel_hailo.c. hailo_open() returns 0 (no model set), 1 (sign) or 2 (verify). */
int hailo_open(void);
void hailo_cs(polyvecl *cs1, polyveck *cs2, const poly *c);
void hailo_ct1(polyveck *ct1, const poly *c);
#endif

#endif
