#include "AVXBrain.h"
#include "helpers.h"
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdalign.h>

void avxbrain_init(struct AVXBrain *b) {
  b->canary = 472472;
  size_t neurons = (BRAIN_WIDTH * BRAIN_DEPTH) / 8;
  size_t weights = (BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH - 1)) / 8;

  const float zero = 0.0f;

  // Init biases and initial vals
  for (size_t i = 0; i < neurons; i++) {
    for (int j = 0; j < 8; j++) {
      b->biases[i][j] = randf(-1.0f, 1.0f);
    }
    b->vals[i] = _mm256_broadcast_ss(&zero);
  }

  // Init weights
  for (size_t i = 0; i < weights; i++) {
    for (int j = 0; j < 8; j++) {
      b->weights[i][j] = randf(-1.0f, 1.0f);
    }
  }

  if (b->canary != 472472) {
	  printf("Canary dead at avxbrain_init\n");
	  sleep(10);
  }
}

void avxbrain_tick(struct AVXBrain *b, float (*inputs)[INPUTSIZE], float(*outputs)[OUTPUTSIZE]) {
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
  if (b->canary != 472472) {
	  printf("Canary dead at avxbrain tick input setup\n");
	  sleep(10);
  }
  // Run input layer manually
  //
  // For each group of 8 neurons
  for (size_t i = 0; i < (BRAIN_WIDTH / 8); i++) {

    __m256 sum = {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};

    // For each group of 8 previous layer neurons
    for (int j = 0; j < BRAIN_WIDTH / 8; j++) {
      __m256 neural_in =
          _mm256_mul_ps(b->inputs[j], b->weights[i * (BRAIN_WIDTH / 8) + j]);
      sum = _mm256_add_ps(sum, neural_in);
    }

    b->vals[i] = _mm256_add_ps(sum, b->biases[i]);

    // ReLu activation function
    b->vals[i] = _mm256_max_ps((__m256) {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} , b->vals[i]);
  }
  if (b->canary != 472472) {
	  printf("Canary dead at avxbrain tick input run\n");
	  sleep(10);
  }

  // For the rest of the hidden layers
  // For each group of 8 neurons
  for (size_t i = (BRAIN_WIDTH / 8); i < (BRAIN_WIDTH * BRAIN_DEPTH) / 8; i++) {
    // What is the current depth
    size_t layer = i / (BRAIN_WIDTH / 8);

    __m256 sum = {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};

    // For each group of 8 previous layer neurons
    for (int j = 0; j < BRAIN_WIDTH / 8; j++) {
      __m256 neural_in = _mm256_mul_ps(b->vals[(layer - 1) * (BRAIN_WIDTH / 8) + j],
                                       b->weights[i * (BRAIN_WIDTH / 8) + j]);
      sum = _mm256_add_ps(sum, neural_in);
    }

    b->vals[i] = _mm256_add_ps(sum, b->biases[i]);

    // ReLu activation function
    b->vals[i] = _mm256_max_ps((__m256) {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f} , b->vals[i]);
  }
  if (b->canary != 472472) {
	  printf("Canary dead at avxbrain tick hidden layers\n");
	  sleep(10);
  }

  size_t output_layer = BRAIN_WIDTH * (BRAIN_DEPTH - 1) / 8;

  for (size_t i = 0; i < BRAIN_WIDTH / 8; i++) {

    //alignas(32) float out_list[8];

    //_mm256_store_ps(out_list, b->vals[output_layer + i]);

    for (size_t j = 0; j < 8; j++) {
      size_t out_idx = i * 8 + j;
      if (out_idx <= OUTPUTSIZE) {
        (*outputs)[out_idx] = fmaxf(fminf(b->vals[output_layer + i][j], 1.0f), 0.0f);
      }
    }
  }
  if (b->canary != 472472) {
	  printf("Canary dead at avxbrain tick output set\n");
	  sleep(10);
  }

}

void avxbrain_mutate(struct AVXBrain *brain, float mutaterate,
                     float mutaterate2) {
        size_t biases = BRAIN_WIDTH * BRAIN_DEPTH / 8;
        size_t weights = BRAIN_WIDTH * BRAIN_WIDTH * (BRAIN_DEPTH - 1) / 8;

	//printf("Trying mutate\n");
	if (randf(0.0f, 1.0f) > mutaterate) {
	  //printf("m1: %f, m2: %f\n", mutaterate, mutaterate2);
	  //printf("Will mutate\n");

	  // mutate w
		  size_t idx = randi(0, weights);

		  for (int i = 0; i < 8; i++) {
              		brain->weights[idx][i] = randf(-2.0f, 2.0f);
		  }

	      // mutate b
		  idx = randi(0, biases);

		  for (int i = 0; i < 8; i++) {
              		brain->biases[idx][i] = randf(-2.0f, 2.0f);
		  }
	}
  if (brain->canary != 472472) {
	  printf("Canary dead at avxbrain mutate\n");
	  sleep(10);
  }
}
