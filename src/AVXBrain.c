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

__m256 exp256_ps(__m256 x) {
  /* Modified code from this source: https://github.com/reyoung/avx_mathfun

     AVX implementation of exp
     Based on "sse_mathfun.h", by Julien Pommier
     http://gruntthepeon.free.fr/ssemath/
     Copyright (C) 2012 Giovanni Garberoglio
     Interdisciplinary Laboratory for Computational Science (LISC)
     Fondazione Bruno Kessler and University of Trento
     via Sommarive, 18
     I-38123 Trento (Italy)
    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
    (this is the zlib license)

  */
  /*
    To increase the compatibility across different compilers the original code
    is converted to plain AVX2 intrinsics code without ingenious macro's, gcc
    style alignment attributes etc. Moreover, the part "express exp(x) as exp(g
    + n*log(2))" has been significantly simplified. This modified code is not
    thoroughly tested!
  */

  __m256 exp_hi = _mm256_set1_ps(88.3762626647949f);
  __m256 exp_lo = _mm256_set1_ps(-88.3762626647949f);

  __m256 cephes_LOG2EF = _mm256_set1_ps(1.44269504088896341f);
  __m256 inv_LOG2EF = _mm256_set1_ps(0.693147180559945f);

  __m256 cephes_exp_p0 = _mm256_set1_ps(1.9875691500E-4);
  __m256 cephes_exp_p1 = _mm256_set1_ps(1.3981999507E-3);
  __m256 cephes_exp_p2 = _mm256_set1_ps(8.3334519073E-3);
  __m256 cephes_exp_p3 = _mm256_set1_ps(4.1665795894E-2);
  __m256 cephes_exp_p4 = _mm256_set1_ps(1.6666665459E-1);
  __m256 cephes_exp_p5 = _mm256_set1_ps(5.0000001201E-1);
  __m256 fx;
  __m256i imm0;
  __m256 one = _mm256_set1_ps(1.0f);

  x = _mm256_min_ps(x, exp_hi);
  x = _mm256_max_ps(x, exp_lo);

  /* express exp(x) as exp(g + n*log(2)) */
  fx = _mm256_mul_ps(x, cephes_LOG2EF);
  fx = _mm256_round_ps(fx, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
  __m256 z = _mm256_mul_ps(fx, inv_LOG2EF);
  x = _mm256_sub_ps(x, z);
  z = _mm256_mul_ps(x, x);

  __m256 y = cephes_exp_p0;
  y = _mm256_mul_ps(y, x);
  y = _mm256_add_ps(y, cephes_exp_p1);
  y = _mm256_mul_ps(y, x);
  y = _mm256_add_ps(y, cephes_exp_p2);
  y = _mm256_mul_ps(y, x);
  y = _mm256_add_ps(y, cephes_exp_p3);
  y = _mm256_mul_ps(y, x);
  y = _mm256_add_ps(y, cephes_exp_p4);
  y = _mm256_mul_ps(y, x);
  y = _mm256_add_ps(y, cephes_exp_p5);
  y = _mm256_mul_ps(y, z);
  y = _mm256_add_ps(y, x);
  y = _mm256_add_ps(y, one);

  /* build 2^n */
  imm0 = _mm256_cvttps_epi32(fx);
  imm0 = _mm256_add_epi32(imm0, _mm256_set1_epi32(0x7f));
  imm0 = _mm256_slli_epi32(imm0, 23);
  __m256 pow2n = _mm256_castsi256_ps(imm0);
  y = _mm256_mul_ps(y, pow2n);
  return y;
}

__m256 activation_function(__m256 x) {
  // printf("input before: %f\n", x[0]);

  __m256 minusone = _mm256_set1_ps(-1.0f);
  __m256 one = _mm256_set1_ps(1.0f);

  // flip sign
  x = _mm256_mul_ps(
      x, minusone);

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
  x = _mm256_add_ps(x,
                    one);
  x = _mm256_div_ps(one,
                    x);

  // printf("activation after: %f\n", x[0]);
  return x;
}

void avxbrain_init(struct AVXBrain *b) {
  size_t neurons = (BRAIN_WIDTH * BRAIN_DEPTH) / 8;
  size_t weights = (BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH - 1)) / 8;

  // Init weights
  for (size_t i = 0; i < weights; i++) {
    alignas(32) float randvals[8] = {
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
      randf(-WEIGHT_RANGE, WEIGHT_RANGE),
    };
      b->weights[i] = _mm256_load_ps((const float*) randvals);
  }
  
  // Init biases and initial vals
  for (size_t i = 0; i < neurons; i++) {
    alignas(32) float randvals[8] = {
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
      randf(-BIAS_RANGE, BIAS_RANGE),
    };
    b->biases[i] = _mm256_load_ps((const float*) randvals);
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
    __m256 sum = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // For each group of 8 previous layer neurons
    for (int j = 0; j < BRAIN_WIDTH / 8; j++) {
      size_t weight_group_idx = i * (BRAIN_WIDTH / 8) + j;
      size_t input_group_idx = j;

      __m256 neural_in = _mm256_mul_ps(b->inputs[input_group_idx],
                                       b->weights[weight_group_idx]);
      sum = _mm256_add_ps(sum, neural_in);
    }

    b->vals[i] = _mm256_add_ps(sum, b->biases[i]);

    // sigmoid activation function
    b->vals[i] = activation_function(b->vals[i]);
  }

  // For the rest of the hidden layers
  // For each group of 8 neurons
  for (size_t i = (BRAIN_WIDTH / 8); i < (BRAIN_WIDTH * BRAIN_DEPTH) / 8; i++) {
    // What is the current depth
    __m256 sum = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    size_t prev_layer = (i / (BRAIN_WIDTH / 8)) - 1;

    // For each group of 8 previous layer neurons
    for (int j = 0; j < BRAIN_WIDTH / 8; j++) {
      size_t weight_group_idx = i * (BRAIN_WIDTH / 8) + j;
      size_t val_group_idx = prev_layer * (BRAIN_WIDTH / 8) + j;

      __m256 neural_in =
          _mm256_mul_ps(b->vals[val_group_idx], b->weights[weight_group_idx]);
      sum = _mm256_add_ps(sum, neural_in);
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
    size_t numbtomut = randi(1,5);
    size_t numwtomut = randi(1,25);
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
