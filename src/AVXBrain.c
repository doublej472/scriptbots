#include "AVXBrain.h"
#include "helpers.h"
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <simde/x86/avx.h>
#include <simde/x86/avx512.h>

#include "AVXBrain.h"

// Clamped ReLu
// Range of 0.0f to 1.0f
static inline simde__m512 activation_function(simde__m512 x) {
  // printf("input before: %f\n", x[0]);
  x = simde_mm512_max_ps(x, simde_mm512_set1_ps(0.0f));
  x = simde_mm512_min_ps(x, simde_mm512_set1_ps(1.0f));
  return x;
}

void avxbrain_init_zero(struct AVXBrain *b) {
  // For each layer
  for (size_t i = 0; i < BRAIN_DEPTH; i++) {
    // Set inputs
    for (size_t j = 0; j < BRAIN_WIDTH; j++) {
      b->layers[i].inputs[j] = simde_mm512_set1_ps(0.0f);
    }

    for (size_t j = 0; j < BRAIN_WIDTH; j++) {
      // Set biases
      b->layers[i].biases[j] = simde_mm512_set1_ps(0.0f);
    }

    // set weights
    for (size_t j = 0; j < BRAIN_WEIGHTS; j++) {
      b->layers[i].weights[j] = simde_mm512_set1_ps(0.0f);
    }
  }
}

void avxbrain_init_random(struct AVXBrain *b) {
  // For each layer
  for (size_t i = 0; i < BRAIN_DEPTH; i++) {
    for (size_t j = 0; j < BRAIN_WIDTH; j++) {
      b->layers[i].inputs[j] = simde_mm512_set1_ps(0.0f);
    }

    for (size_t j = 0; j < BRAIN_WIDTH; j++) {
      // Set biases
      for (size_t k = 0; k < BRAIN_ELEMENTS_PER_VECTOR; k++) {
        b->layers[i].biases[j][k] =
            (randf(0.0f, 1.0f) - 0.5f) * BRAIN_BIAS_RANGE * 2;
      }
    }

    for (size_t j = 0; j < BRAIN_WEIGHTS; j++) {
      for (size_t k = 0; k < BRAIN_ELEMENTS_PER_VECTOR; k++) {
        b->layers[i].weights[j][k] =
            (randf(0.0f, 1.0f) - 0.5f) * BRAIN_WEIGHT_RANGE * 2;
      }
    }
  }
}

void avxbrain_tick(struct AVXBrain *b, float (*brain_inputs)[BRAIN_INPUT_SIZE],
                   float (*brain_outputs)[BRAIN_OUTPUT_SIZE]) {
  // Set the inputs for the first layer
  for (int i = 0; i < BRAIN_INPUT_SIZE; i++) {
    b->layers[0]
        .inputs[i / BRAIN_ELEMENTS_PER_VECTOR][i % BRAIN_ELEMENTS_PER_VECTOR] =
        (*brain_inputs)[i];
  }

  // Tick the brain
  // For each layer
  for (size_t i = 0; i < BRAIN_DEPTH; i++) {
    // Store the layer inputs in inputs variable
    struct AVXBrainLayer *layer = &b->layers[i];

    simde__m512 sum[BRAIN_WIDTH] = {simde_mm512_set1_ps(0.0f)};

    // For each input
    for (size_t j = 0; j < BRAIN_WIDTH_ELEMENTS; j++) {
      // The current input value we are working with
      simde__m512 input =
          simde_mm512_set1_ps(layer->inputs[j / BRAIN_ELEMENTS_PER_VECTOR]
                                           [j % BRAIN_ELEMENTS_PER_VECTOR]);

      // For each group of weights
      for (size_t k = 0; k < BRAIN_WIDTH; k++) {
        sum[k] = simde_mm512_add_ps(
            simde_mm512_mul_ps(input, layer->weights[(j * BRAIN_WIDTH) + k]),
            sum[k]);
      }
    }
    for (size_t k = 0; k < BRAIN_WIDTH; k++) {
      // Biases
      sum[k] = simde_mm512_add_ps(sum[k], layer->biases[k]);
      // Activation

      sum[k] = activation_function(sum[k]);
    }

    // If we are on the last layer write to output, otherwise write to the
    // input of the next layer
    if (i == BRAIN_DEPTH - 1) {
      for (size_t j = 0; j < BRAIN_OUTPUT_SIZE; j++) {
        (*brain_outputs)[j] =
            sum[j / BRAIN_ELEMENTS_PER_VECTOR][j % BRAIN_ELEMENTS_PER_VECTOR];
      }
    } else {
      for (size_t j = 0; j < BRAIN_WIDTH; j++) {
        b->layers[i + 1].inputs[j] = sum[j];
      }
    }
  }
}

/// <summary>
///   Mutate the brain
/// </summary>
/// <param name="brain">pointer to AVXBrain</param>
/// <param name="mutaterate">chance for a given element to mutate</param>
/// <param name="mutaterate2">magnitute of each mutation</param>
void avxbrain_mutate(struct AVXBrain *b, float mutaterate, float mutaterate2) {
  // For each layer
  for (size_t i = 0; i < BRAIN_DEPTH; i++) {

    // For biases
    for (size_t j = 0; j < BRAIN_WIDTH; j++) {
      for (size_t k = 0; k < BRAIN_ELEMENTS_PER_VECTOR; k++) {
        if (randf(0.0f, 1.0f) < mutaterate) {
          float in = b->layers[i].biases[j][k];
          in += (randf(0.0f, 1.0f) - 0.5f) * mutaterate2 * 2;
          if (in > BRAIN_BIAS_RANGE) {
            in = BRAIN_BIAS_RANGE;
          } else if (in < -BRAIN_BIAS_RANGE) {
            in = -BRAIN_BIAS_RANGE;
          }

          b->layers[i].biases[j][k] = in;
        }
      }
    }

    // For weights
    for (size_t j = 0; j < BRAIN_WEIGHTS; j++) {
      for (size_t k = 0; k < BRAIN_ELEMENTS_PER_VECTOR; k++) {
        if (randf(0.0f, 1.0f) < mutaterate) {
          float in = b->layers[i].weights[j][k];
          in += (randf(0.0f, 1.0f) - 0.5f) * mutaterate2 * 2;
          if (in > BRAIN_WEIGHT_RANGE) {
            in = BRAIN_WEIGHT_RANGE;
          } else if (in < -BRAIN_WEIGHT_RANGE) {
            in = -BRAIN_WEIGHT_RANGE;
          }

          b->layers[i].weights[j][k] = in;
        }
      }
    }
  }
}
