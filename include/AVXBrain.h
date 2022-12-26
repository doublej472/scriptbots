#ifndef MLPBRAIN_H
#define MLPBRAIN_H
#include <stdint.h>
#include <stddef.h>
#include <immintrin.h>
#include <stdalign.h>

#include "helpers.h"
#include "settings.h"

struct AVXBrain {
  alignas(32) __m256 inputs[BRAIN_WIDTH / 8];

  // for each neuron, we have BRAIN_WIDTH inputs that are individually weighted
  alignas(32) __m256 weights[(BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH - 1)) / 8];
  // for each neuron, bias output
  alignas(32) __m256 biases[(BRAIN_WIDTH * BRAIN_DEPTH) / 8];
  // the output value of a given neuron
  // Also includes outputs for the entire brain
  alignas(32) __m256 vals[(BRAIN_WIDTH * BRAIN_DEPTH) / 8];
  //float outputs[OUTPUTSIZE];
  int canary;
};

void avxbrain_init(struct AVXBrain *brain);
void avxbrain_tick(struct AVXBrain *brain, float(*inputs)[INPUTSIZE], float(*outputs)[OUTPUTSIZE]);
void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2);

#endif
