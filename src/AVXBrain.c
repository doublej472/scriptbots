#include "AVXBrain.h"
#include "helpers.h"
#include <immintrin.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// How much the connection weight can vary
static const float WEIGHT_RANGE = 0.8f;

// How much the bias can vary from init
static const float BIAS_RANGE = 1.0f;

// How much the bias can vary because of learning
static const float LEARNED_BIAS_RANGE = 0.9f;

// How much bias can change / 2 on a given brain evaluation
static const float LEARN_RANGE = 0.008f;

static inline float sum8(__m256 x) {
  // hiQuad = ( x7, x6, x5, x4 )
  const __m128 hiQuad = _mm256_extractf128_ps(x, 1);
  // loQuad = ( x3, x2, x1, x0 )
  const __m128 loQuad = _mm256_castps256_ps128(x);
  // sumQuad = ( x3 + x7, x2 + x6, x1 + x5, x0 + x4 )
  const __m128 sumQuad = _mm_add_ps(loQuad, hiQuad);
  // loDual = ( -, -, x1 + x5, x0 + x4 )
  const __m128 loDual = sumQuad;
  // hiDual = ( -, -, x3 + x7, x2 + x6 )
  const __m128 hiDual = _mm_movehl_ps(sumQuad, sumQuad);
  // sumDual = ( -, -, x1 + x3 + x5 + x7, x0 + x2 + x4 + x6 )
  const __m128 sumDual = _mm_add_ps(loDual, hiDual);
  // lo = ( -, -, -, x0 + x2 + x4 + x6 )
  const __m128 lo = sumDual;
  // hi = ( -, -, -, x1 + x3 + x5 + x7 )
  const __m128 hi = _mm_shuffle_ps(sumDual, sumDual, 0x1);
  // sum = ( -, -, -, x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 )
  const __m128 sum = _mm_add_ss(lo, hi);
  return _mm_cvtss_f32(sum);
}

static inline __m256 activation_function(__m256 x) {
  // printf("input before: %f\n", x[0]);
  x = _mm256_max_ps(x,
                    (__m256){0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
  x = _mm256_min_ps(x,
                    (__m256){1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
  return x;
}

void avxbrain_reset_offsets(struct AVXBrain *b) {
  // printf("Offsets reset: ");
  for (size_t i = 0; i < (BRAIN_WIDTH * BRAIN_DEPTH) / 8; i++) {
    b->biases_offset[i] =
        (__m256){0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  }
  // printf("\n");
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
    for (int j = 0; j < 8; j++) {
      b->biases[i][j] = randf(-BIAS_RANGE, BIAS_RANGE);
      b->biases_learnrate[i][j] = randf(-LEARN_RANGE, LEARN_RANGE);
    }
    b->biases_offset[i] =
        (__m256){0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    b->vals[i] = (__m256){0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
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

    // For each neuron
    for (int j = 0; j < 8; j++) {
      __m256 innersum = _mm256_set1_ps(0.0f);
      // for each group of 8 inputs
      for (int k = 0; k < BRAIN_WIDTH / 8; k++) {
        size_t input_group_idx = k;
        size_t weight_group_idx = i * (BRAIN_WIDTH / 8) + j * 8 + k;
        innersum = _mm256_add_ps(_mm256_mul_ps(b->inputs[input_group_idx],
                                               b->weights[weight_group_idx]),
                                 innersum);
      }
      sum[j] = innersum[0] + innersum[1] + innersum[2] + innersum[3] +
               innersum[4] + innersum[5] + innersum[6] + innersum[7];
    }

    b->vals[i] =
        _mm256_add_ps(_mm256_add_ps(sum, b->biases[i]), b->biases_offset[i]);

    // activation function
    b->vals[i] = activation_function(b->vals[i]);

    // Get sum and multiply by the learnrate
    __m256 bias_tmp = _mm256_mul_ps(
        _mm256_sub_ps(b->vals[i],
                      (__m256){0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}),
        b->biases_learnrate[i]);
    // printf("biases_offset[0] before: %.5f", b->biases_offset[i][0]);
    //  "learn"
    b->biases_offset[i] = _mm256_add_ps(b->biases_offset[i], bias_tmp);

    // Clamp range
    b->biases_offset[i] = _mm256_max_ps(
        b->biases_offset[i],
        (__m256){-LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE,
                 -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE,
                 -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE});
    b->biases_offset[i] = _mm256_min_ps(
        b->biases_offset[i],
        (__m256){LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE,
                 LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE,
                 LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE});
    // printf(" after: %.5f\n", b->biases_offset[i][0]);
  }

  // For the rest of the hidden layers
  // For each group of 8 neurons
  for (size_t i = (BRAIN_WIDTH / 8); i < (BRAIN_WIDTH * BRAIN_DEPTH) / 8; i++) {
    // What is the current depth
    __m256 sum = _mm256_set1_ps(0.0f);

    size_t prev_layer = (i / (BRAIN_WIDTH / 8)) - 1;

    // For each neuron
    for (int j = 0; j < 8; j++) {
      __m256 innersum = _mm256_set1_ps(0.0f);
      // for each group of 8 inputs
      for (int k = 0; k < BRAIN_WIDTH / 8; k++) {
        size_t val_group_idx = prev_layer * (BRAIN_WIDTH / 8) + j;
        size_t weight_group_idx = i * (BRAIN_WIDTH / 8) + j * 8 + k;
        innersum = _mm256_add_ps(
            _mm256_mul_ps(b->vals[val_group_idx], b->weights[weight_group_idx]),
            innersum);
      }
      sum[j] = innersum[0] + innersum[1] + innersum[2] + innersum[3] +
               innersum[4] + innersum[5] + innersum[6] + innersum[7];
    }

    b->vals[i] =
        _mm256_add_ps(_mm256_add_ps(sum, b->biases[i]), b->biases_offset[i]);

    // activation function
    b->vals[i] = activation_function(b->vals[i]);

    // Multiply by the learnrate
    __m256 bias_tmp = _mm256_mul_ps(
        _mm256_sub_ps(b->vals[i],
                      (__m256){0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}),
        b->biases_learnrate[i]);
    // "learn"
    b->biases_offset[i] = _mm256_add_ps(b->biases_offset[i], bias_tmp);

    // Clamp range
    b->biases_offset[i] = _mm256_max_ps(
        b->biases_offset[i],
        (__m256){-LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE,
                 -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE,
                 -LEARNED_BIAS_RANGE, -LEARNED_BIAS_RANGE});
    b->biases_offset[i] = _mm256_min_ps(
        b->biases_offset[i],
        (__m256){LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE,
                 LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE,
                 LEARNED_BIAS_RANGE, LEARNED_BIAS_RANGE});
  }

  size_t output_layer = BRAIN_WIDTH * (BRAIN_DEPTH - 1) / 8;

  for (size_t i = 0; i < BRAIN_WIDTH / 8; i++) {
    for (size_t j = 0; j < 8; j++) {
      size_t out_idx = i * 8 + j;
      if (out_idx < OUTPUTSIZE) {
        float desired_output =
            fmaxf(fminf(b->vals[output_layer + i][j], 1.0f), 0.0f);
        // Go 75% of the way to the desired output
        (*outputs)[out_idx] =
            (*outputs)[out_idx] * 0.33f + desired_output * 0.67f;
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
    size_t numlrtomut = randi(1, 25);
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
    for (int i = 0; i < numlrtomut; i++) {
      size_t idx = randi(0, biases);
      brain->biases_learnrate[idx / 8][idx % 8] +=
          randf(-mutaterate2, mutaterate2);
      if (brain->biases_learnrate[idx / 8][idx % 8] > LEARN_RANGE) {
        brain->biases_learnrate[idx / 8][idx % 8] = LEARN_RANGE;
      } else if (brain->biases_learnrate[idx / 8][idx % 8] < -LEARN_RANGE) {
        brain->biases_learnrate[idx / 8][idx % 8] = -LEARN_RANGE;
      }
    }
  }
}
