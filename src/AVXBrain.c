#include "AVXBrain.h"
#include "helpers.h"
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <simde/x86/avx.h>

// Clamped ReLu
// Range of 0.0f to 1.0f
static inline __m256 activation_function(__m256 x) {
  // printf("input before: %f\n", x[0]);
  x = _mm256_max_ps(x, _mm256_set1_ps(0.0f));
  x = _mm256_min_ps(x, _mm256_set1_ps(1.0f));
  return x;
}

void avxbrain_init(struct AVXBrain *b) {
  // For each neuron group
  for (size_t i = 0; i < BRAIN_DEPTH; i++) {
    for (size_t j = 0; j < BRAIN_WIDTH / 8; j++) {
      struct AVXBrainGroup *ng = &b->layers[i].groups[j];
      // Zero inputs
      b->layers[i].inputs[j] = _mm256_set1_ps(0.0f);

      // Set biases
      for (size_t k = 0; k < 8; k++) {
        ng->biases[k] = randf(-BIAS_RANGE, BIAS_RANGE);
      }

      // Set weights
      for (size_t wg = 0; wg < BRAIN_WIDTH; wg++) {
        for (size_t k = 0; k < 8; k++) {
          ng->weights[wg][k] = randf(-WEIGHT_RANGE, WEIGHT_RANGE);
        }
      }
    }
  }
}

void avxbrain_tick(struct AVXBrain *b, float (*brain_inputs)[INPUTSIZE],
                   float (*brain_outputs)[OUTPUTSIZE]) {
  // Set the inputs for the first layer
  for (int i = 0; i < INPUTSIZE; i++) {
    b->layers[0].inputs[i / 8][i % 8] = (*brain_inputs)[i];
  }

  // Tick the brain!
  // For each layer
  for (size_t i = 0; i < BRAIN_DEPTH; i++) {
    // Store the layer inputs in inputs variable
    struct AVXBrainLayer *layer = &b->layers[i];

    // For each brain group (~group of 8 neurons)
    for (size_t j = 0; j < BRAIN_WIDTH / 8; j++) {
      struct AVXBrainGroup *ng = &layer->groups[j];
      __m256 sum = _mm256_set1_ps(0.0f);

      // For each individual neuron
      for (size_t k = 0; k < 8; k++) {
        __m256 innersum = _mm256_set1_ps(0.0f);

        // For each input group
        for (size_t l = 0; l < BRAIN_WIDTH / 8; l++) {
          innersum = _mm256_add_ps(
              _mm256_mul_ps(layer->inputs[l], ng->weights[(l * 8) + k]),
              innersum);
        }

        // Let compiler optimize this instead of trying to make horizontal sum work
        sum[k] = innersum[0] + innersum[1] + innersum[2] + innersum[3] +
                 innersum[4] + innersum[5] + innersum[6] + innersum[7];
      }

      // Apply biases
      __m256 finalsum =
          _mm256_add_ps(sum, ng->biases);

      // Send sum to activation function
      finalsum = activation_function(finalsum);

      // If we are on the last layer write to output, otherwise write to the
      // input of the next layer
      if (i == BRAIN_DEPTH - 1) {
        for (size_t k = 0; k < 8; k++) {
          (*brain_outputs)[j * 8 + k] = finalsum[k];
        }
      } else {
        b->layers[i + 1].inputs[j] = finalsum;
      }
    }
  }
}

void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2) {
  size_t biases = BRAIN_WIDTH * 8 * BRAIN_DEPTH;
  size_t weights = BRAIN_DEPTH * BRAIN_WIDTH * 8 * BRAIN_WIDTH;

  // printf("Trying mutate\n");
  if (randf(0.0f, 1.0f) > mutaterate) {
    size_t numbtomut = randf(0.0f, 0.2f) * biases;
    size_t numwtomut = randf(0.0f, 0.2f) * weights;
    // printf("m1: %f, m2: %f\n", mutaterate, mutaterate2);
    // printf("Will mutate\n");

    for (int i = 0; i < numbtomut; i++) {
      size_t layer = randi(0, BRAIN_DEPTH);
      size_t ng = randi(0, BRAIN_WIDTH / 8);
      size_t elem = randi(0, 8);

      float inval = brain->layers[layer].groups[ng].biases[elem];

      inval += randf(-mutaterate2, mutaterate2);

      if (inval > BIAS_RANGE) {
        inval = BIAS_RANGE;
      } else if (inval < -BIAS_RANGE) {
        inval = -BIAS_RANGE;
      }

      brain->layers[layer].groups[ng].biases[elem] = inval;
    }
    // mutate w
    for (int i = 0; i < numwtomut; i++) {
      size_t layer = randi(0, BRAIN_DEPTH);
      size_t ng = randi(0, BRAIN_WIDTH / 8);
      size_t wg = randi(0, BRAIN_WIDTH);
      size_t elem = randi(0, 8);

      float inval = brain->layers[layer].groups[ng].weights[wg][elem];

      inval += randf(-mutaterate2, mutaterate2);

      if (inval > WEIGHT_RANGE) {
        inval = WEIGHT_RANGE;
      } else if (inval < -WEIGHT_RANGE) {
        inval = -WEIGHT_RANGE;
      }

      brain->layers[layer].groups[ng].weights[wg][elem] = inval;
    }
  }
}
