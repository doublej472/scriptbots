#ifndef MLPBRAIN_H
#define MLPBRAIN_H
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include <simde/x86/avx.h>

#include "helpers.h"
#include "settings.h"

// How much the connection weight can vary
static const float WEIGHT_RANGE = 0.8f;

// How much the bias can vary from init
static const float BIAS_RANGE = 1.0f;

struct AVXBrainGroup {
  alignas(32) __m256 biases;
  // This needs to point at every possible neuron on the previous layer
  alignas(32) __m256 weights[BRAIN_WIDTH];
};

struct AVXBrainLayer {
  alignas(32) struct AVXBrainGroup groups[BRAIN_WIDTH / 8];
};

struct AVXBrain {
  alignas(32) struct AVXBrainLayer layers[BRAIN_DEPTH];
};

void avxbrain_init(struct AVXBrain *brain);
void avxbrain_tick(struct AVXBrain *brain, float (*inputs)[INPUTSIZE],
                   float (*outputs)[OUTPUTSIZE]);
void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2);

#endif
