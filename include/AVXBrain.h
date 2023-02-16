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

// How much the bias can vary because of learning
static const float LEARNED_BIAS_RANGE = 0.9f;

// How much bias can change / 2 on a given brain evaluation
static const float LEARN_RANGE = 0.008f;

struct AVXBrainGroup {
  alignas(32) __m256 biases;
  alignas(32) __m256 biases_offset;
  alignas(32) __m256 biases_learnrate;
  // This needs to point at every possible neuron on the previous layer
  alignas(32) __m256 weights[BRAIN_WIDTH];
};

struct AVXBrainLayer {
  alignas(32) __m256 inputs[BRAIN_WIDTH / 8];
  alignas(32) struct AVXBrainGroup groups[BRAIN_WIDTH / 8];
};

struct AVXBrain {
  alignas(32) struct AVXBrainLayer layers[BRAIN_DEPTH];
};

void avxbrain_init(struct AVXBrain *brain);
void avxbrain_reset_offsets(struct AVXBrain *brain);
void avxbrain_tick(struct AVXBrain *brain, float (*inputs)[INPUTSIZE],
                   float (*outputs)[OUTPUTSIZE]);
void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2);

#endif
