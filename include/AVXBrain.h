#ifndef MLPBRAIN_H
#define MLPBRAIN_H
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include <simde/x86/avx.h>
#include <simde/x86/avx512.h>

#include "helpers.h"
#include "settings.h"

// How much the connection weight can vary
static const float WEIGHT_RANGE = 0.8f;

// How much the bias can vary from init
static const float BIAS_RANGE = 1.0f;

// Internal vector type for brain
typedef simde__m512 AVXBrainVector;

struct SIMDE_AVX512_ALIGN AVXBrainLayer {
  AVXBrainVector SIMDE_AVX512_ALIGN inputs[BRAIN_WIDTH];
  AVXBrainVector SIMDE_AVX512_ALIGN biases[BRAIN_WIDTH];
  // Each BRAIN_WIDTH stride maps to a single input / bias
  AVXBrainVector SIMDE_AVX512_ALIGN weights[BRAIN_WEIGHTS];
};

struct SIMDE_AVX512_ALIGN AVXBrain {
  struct SIMDE_AVX512_ALIGN AVXBrainLayer layers[BRAIN_DEPTH];
};

void avxbrain_init_zero(struct AVXBrain *brain);
void avxbrain_init_random(struct AVXBrain *brain);
void avxbrain_tick(struct AVXBrain *brain, float (*inputs)[BRAIN_INPUT_SIZE],
                   float (*outputs)[BRAIN_OUTPUT_SIZE]);
void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2);

#endif
