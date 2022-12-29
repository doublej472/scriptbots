#include "AVXBrain.h"
#include "helpers.h"
#include <immintrin.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const float WEIGHT_RANGE = 8.0f;
static const float BIAS_RANGE = 3.0f;

#define USE_FMA 1

static inline __m256 exp256_ps(__m256 x) {
  __m256 t, f, p, r;
  __m256i i, j;

  const __m256 l2e = _mm256_set1_ps(1.442695041f);    /* log2(e) */
  const __m256 l2h = _mm256_set1_ps(-6.93145752e-1f); /* -log(2)_hi */
  const __m256 l2l = _mm256_set1_ps(-1.42860677e-6f); /* -log(2)_lo */
  /* coefficients for core approximation to exp() in [-log(2)/2, log(2)/2] */
  const __m256 c0 = _mm256_set1_ps(0.041944388f);
  const __m256 c1 = _mm256_set1_ps(0.168006673f);
  const __m256 c2 = _mm256_set1_ps(0.499999940f);
  const __m256 c3 = _mm256_set1_ps(0.999956906f);
  const __m256 c4 = _mm256_set1_ps(0.999999642f);

  /* exp(x) = 2^i * e^f; i = rint (log2(e) * x), f = x - log(2) * i */
  t = _mm256_mul_ps(x, l2e); /* t = log2(e) * x */
  r = _mm256_round_ps(t, _MM_FROUND_TO_NEAREST_INT |
                             _MM_FROUND_NO_EXC); /* r = rint (t) */

#if USE_FMA
  f = _mm256_fmadd_ps(r, l2h, x); /* x - log(2)_hi * r */
  f = _mm256_fmadd_ps(r, l2l, f); /* f = x - log(2)_hi * r - log(2)_lo * r */
#else                             // USE_FMA
  p = _mm256_mul_ps(r, l2h); /* log(2)_hi * r */
  f = _mm256_add_ps(x, p);   /* x - log(2)_hi * r */
  p = _mm256_mul_ps(r, l2l); /* log(2)_lo * r */
  f = _mm256_add_ps(f, p);   /* f = x - log(2)_hi * r - log(2)_lo * r */
#endif                            // USE_FMA

  i = _mm256_cvtps_epi32(t); /* i = (int)rint(t) */

  /* p ~= exp (f), -log(2)/2 <= f <= log(2)/2 */
  p = c0; /* c0 */
#if USE_FMA
  p = _mm256_fmadd_ps(p, f, c1); /* c0*f+c1 */
  p = _mm256_fmadd_ps(p, f, c2); /* (c0*f+c1)*f+c2 */
  p = _mm256_fmadd_ps(p, f, c3); /* ((c0*f+c1)*f+c2)*f+c3 */
  p = _mm256_fmadd_ps(p, f, c4); /* (((c0*f+c1)*f+c2)*f+c3)*f+c4 ~= exp(f) */
#else                            // USE_FMA
  p = _mm256_mul_ps(p, f);   /* c0*f */
  p = _mm256_add_ps(p, c1);  /* c0*f+c1 */
  p = _mm256_mul_ps(p, f);   /* (c0*f+c1)*f */
  p = _mm256_add_ps(p, c2);  /* (c0*f+c1)*f+c2 */
  p = _mm256_mul_ps(p, f);   /* ((c0*f+c1)*f+c2)*f */
  p = _mm256_add_ps(p, c3);  /* ((c0*f+c1)*f+c2)*f+c3 */
  p = _mm256_mul_ps(p, f);   /* (((c0*f+c1)*f+c2)*f+c3)*f */
  p = _mm256_add_ps(p, c4);  /* (((c0*f+c1)*f+c2)*f+c3)*f+c4 ~= exp(f) */
#endif                           // USE_FMA

  /* exp(x) = 2^i * p */
  j = _mm256_slli_epi32(i, 23); /* i << 23 */
  r = _mm256_castsi256_ps(
      _mm256_add_epi32(j, _mm256_castps_si256(p))); /* r = p * 2^i */

  return r;
}

static inline __m256 activation_function(__m256 x) {
  // printf("input before: %f\n", x[0]);

  // flip sign
  x = _mm256_xor_ps(x, _mm256_set1_ps(-0.0f));

  // exp part
  x = exp256_ps(x);

  //// https://www.musicdsp.org/en/latest/Other/222-fast-exp-approximations.html
  // x = _mm256_add_ps(x, (__m256) {4.0f,4.0f,4.0f,4.0f,4.0f,4.0f,4.0f,4.0f});
  // x = _mm256_mul_ps(x, x);
  // x = _mm256_add_ps(x, (__m256)
  // {12.0f,12.0f,12.0f,12.0f,12.0f,12.0f,12.0f,12.0f}); x = _mm256_mul_ps(x,
  // x); x = _mm256_add_ps(x, (__m256)
  // {24.0f,24.0f,24.0f,24.0f,24.0f,24.0f,24.0f,24.0f}); x = _mm256_mul_ps(x,
  // x); x = _mm256_add_ps(x, (__m256)
  // {24.0f,24.0f,24.0f,24.0f,24.0f,24.0f,24.0f,24.0f}); x = _mm256_mul_ps(x,
  // (__m256)
  // {0.041666666f,0.041666666f,0.041666666f,0.041666666f,0.041666666f,0.041666666f,0.041666666f,0.041666666f,});

  // printf("exp after: %f\n", x[0]);
  //  sigmoid part
  x = _mm256_add_ps(x, _mm256_set1_ps(1.0f));
  x = _mm256_div_ps(_mm256_set1_ps(1.0f), x);

  // printf("activation after: %f\n", x[0]);
  return x;
}

void avxbrain_init(struct AVXBrain *b) {
  size_t neurons = (BRAIN_WIDTH * BRAIN_DEPTH) / 8;
  size_t weights = (BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH)) / 8;

  // Init weights
  for (size_t i = 0; i < weights; i++) {
    alignas(32) float randvals[8] = {
        randf(-WEIGHT_RANGE, WEIGHT_RANGE), randf(-WEIGHT_RANGE, WEIGHT_RANGE),
        randf(-WEIGHT_RANGE, WEIGHT_RANGE), randf(-WEIGHT_RANGE, WEIGHT_RANGE),
        randf(-WEIGHT_RANGE, WEIGHT_RANGE), randf(-WEIGHT_RANGE, WEIGHT_RANGE),
        randf(-WEIGHT_RANGE, WEIGHT_RANGE), randf(-WEIGHT_RANGE, WEIGHT_RANGE),
    };
    b->weights[i] = _mm256_load_ps((const float *)randvals);
  }

  // Init biases and initial vals
  for (size_t i = 0; i < neurons; i++) {
    alignas(32) float randvals[8] = {
        randf(-BIAS_RANGE, BIAS_RANGE), randf(-BIAS_RANGE, BIAS_RANGE),
        randf(-BIAS_RANGE, BIAS_RANGE), randf(-BIAS_RANGE, BIAS_RANGE),
        randf(-BIAS_RANGE, BIAS_RANGE), randf(-BIAS_RANGE, BIAS_RANGE),
        randf(-BIAS_RANGE, BIAS_RANGE), randf(-BIAS_RANGE, BIAS_RANGE),
    };
    b->biases[i] = _mm256_load_ps((const float *)randvals);
  }
}

void avxbrain_tick(struct AVXBrain *b, float (*inputs)[INPUTSIZE],
                   float (*outputs)[OUTPUTSIZE]) {
  // printf("input groups: %d\n", BRAIN_WIDTH / 8);
  // printf("weight groups: %d\n", (BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH -
  // 1)) / 8); printf("bias groups: %d\n", (BRAIN_WIDTH * BRAIN_DEPTH) / 8);
  // printf("val groups: %d\n", (BRAIN_WIDTH * BRAIN_DEPTH) / 8);

  // Number of weights per layer, neurons / biases per layer is BRAIN_WIDTH
  for (size_t i = 0; i < (BRAIN_WIDTH / 8); i++) {
    for (size_t j = 0; j < 8; j++) {
      size_t input_idx = i * 8 + j;
      if (input_idx >= INPUTSIZE) {
        b->inputs[i][j] = 0.0f;
      } else {
        b->inputs[i][j] = (*inputs)[input_idx];
      }
    }
  }
  // Run input layer manually
  //
  // For each group of 8 neurons
  for (size_t i = 0; i < BRAIN_WIDTH / 8; i++) {
    // What is the current depth
    __m256 sum = _mm256_set1_ps(0.0f);

    // For each group of 8 previous layer neurons
    for (int j = 0; j < BRAIN_WIDTH / 8; j++) {
      size_t weight_group_idx = i * (BRAIN_WIDTH / 8) + j;
      size_t input_group_idx = j;

#if USE_FMA
      sum = _mm256_fmadd_ps(b->inputs[input_group_idx],
                            b->weights[weight_group_idx], sum);
#else
      sum = _mm256_add_ps(_mm256_mul_ps(b->inputs[input_group_idx],
                                        b->weights[weight_group_idx]),
                          sum);
#endif
    }

    b->vals[i] = _mm256_add_ps(sum, b->biases[i]);

    // sigmoid activation function
    b->vals[i] = activation_function(b->vals[i]);
  }

  // For the rest of the hidden layers
  // For each group of 8 neurons
  for (size_t i = (BRAIN_WIDTH / 8); i < (BRAIN_WIDTH * BRAIN_DEPTH) / 8; i++) {
    // What is the current depth
    __m256 sum = _mm256_set1_ps(0.0f);

    size_t prev_layer = (i / (BRAIN_WIDTH / 8)) - 1;

    // For each group of 8 previous layer neurons
    for (int j = 0; j < BRAIN_WIDTH / 8; j++) {
      size_t weight_group_idx = i * (BRAIN_WIDTH / 8) + j;
      size_t val_group_idx = prev_layer * (BRAIN_WIDTH / 8) + j;

#if USE_FMA
      sum = _mm256_fmadd_ps(b->inputs[val_group_idx],
                            b->weights[weight_group_idx], sum);
#else
      sum = _mm256_add_ps(
          _mm256_mul_ps(b->inputs[val_group_idx], b->weights[weight_group_idx]),
          sum);
#endif
    }

    b->vals[i] = _mm256_add_ps(sum, b->biases[i]);

    // sigmoid activation function
    b->vals[i] = activation_function(b->vals[i]);
  }

  size_t output_layer = BRAIN_WIDTH * (BRAIN_DEPTH - 1) / 8;
  for (size_t i = 0; i < BRAIN_WIDTH / 8; i++) {
    for (size_t j = 0; j < 8; j++) {
      size_t out_idx = i * 8 + j;
      if (out_idx < OUTPUTSIZE) {
        (*outputs)[out_idx] =
            fmaxf(fminf(b->vals[output_layer + i][j], 1.0f), 0.0f);
      }
    }
  }
}

void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2) {
  size_t biases = BRAIN_WIDTH * BRAIN_DEPTH;
  size_t weights = BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH - 1);

  // printf("Trying mutate\n");
  if (randf(0.0f, 1.0f) > mutaterate) {
    size_t numbtomut = randi(1, 25);
    size_t numwtomut = randi(1, 150);
    // printf("m1: %f, m2: %f\n", mutaterate, mutaterate2);
    // printf("Will mutate\n");

    for (int i = 0; i < numbtomut; i++) {
      size_t idx = randi(0, biases);
      brain->biases[idx / 8][idx % 8] += randf(-mutaterate2, mutaterate2);
      if (brain->biases[idx / 8][idx % 8] > BIAS_RANGE) {
        brain->biases[idx / 8][idx % 8] = BIAS_RANGE;
      } else if (brain->biases[idx / 8][idx % 8] < -BIAS_RANGE) {
        brain->biases[idx / 8][idx % 8] = -BIAS_RANGE;
      }
    }
    // mutate w
    for (int i = 0; i < numwtomut; i++) {
      size_t idx = randi(0, weights);
      brain->weights[idx / 8][idx % 8] += randf(-mutaterate2, mutaterate2);
      if (brain->weights[idx / 8][idx % 8] > WEIGHT_RANGE) {
        brain->weights[idx / 8][idx % 8] = WEIGHT_RANGE;
      } else if (brain->weights[idx / 8][idx % 8] < -WEIGHT_RANGE) {
        brain->weights[idx / 8][idx % 8] = -WEIGHT_RANGE;
      }
    }
  }
}
