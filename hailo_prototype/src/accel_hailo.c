/* Hailo backend: one dense layer, input = challenge c (256 values),
 * output = 2048 products. Uses the HailoRT C API (hailo/hailort.h).
 *
 * NOT compiled on a Mac (no HailoRT there). If a function name or argument
 * differs in your HailoRT version, compare with examples/c/vstreams_example
 * in the HailoRT repo -- this file follows that example's sequence. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <hailo/hailort.h>
#include "accel.h"

#define OUT_LEN 2048 /* sign: (L+K)*N = 8*256; verify: 2*K*N = 8*256 */

#define CHECK(x) do { hailo_status s_ = (x); if(s_ != HAILO_SUCCESS) { \
  fprintf(stderr, "hailo: %s failed, status %d\n", #x, (int)s_); exit(1); } } while(0)

static struct {
  int kind; /* 0 = not opened, 1 = sign model, 2 = verify model, -1 = no model set */
  hailo_vdevice vdevice;
  hailo_hef hef;
  hailo_configured_network_group ng;
  hailo_activated_network_group active;
  hailo_input_vstream in;
  hailo_output_vstream out;
} H;

int hailo_open(void)
{
  const char *sign = getenv("ACCEL_SIGN_HEF");
  const char *verify = getenv("ACCEL_VERIFY_HEF");
  const char *path = sign ? sign : verify;
  hailo_vdevice_params_t vparams;
  hailo_configure_params_t cparams;
  hailo_input_vstream_params_by_name_t iparams[1];
  hailo_output_vstream_params_by_name_t oparams[1];
  size_t n_ng = 1, n_in = 1, n_out = 1, frame;

  if(H.kind) return H.kind < 0 ? 0 : H.kind;
  if(sign && verify) {
    fprintf(stderr, "hailo: set only ONE of ACCEL_SIGN_HEF / ACCEL_VERIFY_HEF\n");
    exit(1);
  }
  if(!path) { H.kind = -1; return 0; }

  CHECK(hailo_init_vdevice_params(&vparams));
  /* Single model, so no scheduler: we activate the network group ourselves. */
  vparams.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
  CHECK(hailo_create_vdevice(&vparams, &H.vdevice));
  CHECK(hailo_create_hef_file(&H.hef, path));
  CHECK(hailo_init_configure_params_by_vdevice(H.hef, H.vdevice, &cparams));
  CHECK(hailo_configure_vdevice(H.vdevice, H.hef, &cparams, &H.ng, &n_ng));

  /* FLOAT32 on both sides: HailoRT does the (de)quantization for us. */
  CHECK(hailo_make_input_vstream_params(H.ng, false, HAILO_FORMAT_TYPE_FLOAT32, iparams, &n_in));
  CHECK(hailo_make_output_vstream_params(H.ng, false, HAILO_FORMAT_TYPE_FLOAT32, oparams, &n_out));
  if(n_in != 1 || n_out != 1) {
    fprintf(stderr, "hailo: model must have exactly 1 input and 1 output\n");
    exit(1);
  }
  CHECK(hailo_create_input_vstreams(H.ng, iparams, n_in, &H.in));
  CHECK(hailo_create_output_vstreams(H.ng, oparams, n_out, &H.out));
  CHECK(hailo_activate_network_group(H.ng, NULL, &H.active));

  CHECK(hailo_get_input_vstream_frame_size(H.in, &frame));
  if(frame != N * sizeof(float)) {
    fprintf(stderr, "hailo: input is %zu bytes, expected %zu\n", frame, N * sizeof(float));
    exit(1);
  }
  CHECK(hailo_get_output_vstream_frame_size(H.out, &frame));
  if(frame != OUT_LEN * sizeof(float)) {
    fprintf(stderr, "hailo: output is %zu bytes, expected %zu\n", frame, OUT_LEN * sizeof(float));
    exit(1);
  }

  H.kind = sign ? 1 : 2;
  return H.kind;
}

/* Send c, get 2048 values back. The true answers are integers, so rounding
 * makes them exact as long as the Hailo's error stays below 0.5. */
static void run(const poly *c, int32_t out[OUT_LEN])
{
  static float in[N], res[OUT_LEN];
  int i;
  for(i = 0; i < N; i++)
    in[i] = (float)c->coeffs[i];
  CHECK(hailo_vstream_write_raw_buffer(H.in, in, sizeof in));
  CHECK(hailo_vstream_read_raw_buffer(H.out, res, sizeof res));
  for(i = 0; i < OUT_LEN; i++)
    out[i] = (int32_t)lrintf(res[i]);
}

/* Output layout (must match hailo/make_model.py): s1[0..L-1] then s2[0..K-1]. */
void hailo_cs(polyvecl *cs1, polyveck *cs2, const poly *c)
{
  int32_t out[OUT_LEN];
  int i, j;
  run(c, out);
  for(i = 0; i < L; i++)
    for(j = 0; j < N; j++)
      cs1->vec[i].coeffs[j] = out[i * N + j];
  for(i = 0; i < K; i++)
    for(j = 0; j < N; j++)
      cs2->vec[i].coeffs[j] = out[(L + i) * N + j];
}

/* Output layout: c*t1_lo for K polys, then c*t1_hi for K polys,
 * where t1 = 32*t1_hi + t1_lo (split so every output fits in 16 bits). */
void hailo_ct1(polyveck *ct1, const poly *c)
{
  int32_t out[OUT_LEN];
  int i, j;
  run(c, out);
  for(i = 0; i < K; i++)
    for(j = 0; j < N; j++)
      ct1->vec[i].coeffs[j] = out[i * N + j] + 32 * out[(K + i) * N + j];
}
