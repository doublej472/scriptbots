#ifndef MLPBRAIN_H
#define MLPBRAIN_H

#include <simde/x86/avx.h>
#include <simde/x86/avx512.h>

// How many hidden layers this brain has
#define BRAIN_DEPTH 3

// How many neuron groups exist in a given layer, exact number depends on datatype
#define BRAIN_WIDTH 4

// Internal vector type for brain
typedef simde__m512 AVXBrainVector;

// How many elements are in a given vector
#define BRAIN_ELEMENTS_PER_VECTOR (sizeof(AVXBrainVector) / 8)

// How many elments are on a given layer
#define BRAIN_WIDTH_ELEMENTS (BRAIN_WIDTH * BRAIN_ELEMENTS_PER_VECTOR)

// How many weights does the brain have (number of neurons * inputs)
#define BRAIN_WEIGHTS (BRAIN_WIDTH_ELEMENTS * BRAIN_WIDTH)

// How many inputs / outputs are dedicated to planning
// which means this number of outputs will be copied to inputs
// on the next brain tick
#define BRAIN_PLANSIZE (BRAIN_WIDTH_ELEMENTS - 19)

// 19 and 18 here represent "real" inputs and outputs
#define BRAIN_INPUT_SIZE (19 + BRAIN_PLANSIZE)
#define BRAIN_OUTPUT_SIZE (18 + BRAIN_PLANSIZE)

// How much the connection weight can vary
#define BRAIN_WEIGHT_RANGE 0.8f

// How much the bias can vary from init
#define BRAIN_BIAS_RANGE 1.0f


SIMDE_AVX512_ALIGN struct AVXBrainLayer {
  SIMDE_AVX512_ALIGN AVXBrainVector inputs[BRAIN_WIDTH];
  SIMDE_AVX512_ALIGN AVXBrainVector biases[BRAIN_WIDTH];
  // Each BRAIN_WIDTH stride maps to a single input / bias
  SIMDE_AVX512_ALIGN AVXBrainVector weights[BRAIN_WEIGHTS];
};

SIMDE_AVX512_ALIGN struct AVXBrain {
  SIMDE_AVX512_ALIGN struct AVXBrainLayer layers[BRAIN_DEPTH];
};

void avxbrain_print(struct AVXBrain* b);
void avxbrain_init_zero(struct AVXBrain *brain);
void avxbrain_init_random(struct AVXBrain* brain);
void avxbrain_tick(struct AVXBrain *brain, float (*inputs)[BRAIN_INPUT_SIZE],
                   float (*outputs)[BRAIN_OUTPUT_SIZE]);
void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2);

#endif
