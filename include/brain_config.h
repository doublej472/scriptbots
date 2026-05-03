// brain_config.h — neural network topology and GPU-compatible layout constants
#ifndef BRAIN_CONFIG_H
#define BRAIN_CONFIG_H

// How many hidden layers this brain has
#define BRAIN_LAYERS 3

// How many neurons per layer (padded to GPU workgroup size)
#define NEURONS_PER_LAYER 48

// How many floats per layer in GPU layout: weights[48×48] + biases[48]
#define WEIGHTS_PER_LAYER  (NEURONS_PER_LAYER * NEURONS_PER_LAYER)
#define BIASES_PER_LAYER    NEURONS_PER_LAYER
#define FLOATS_PER_LAYER    (WEIGHTS_PER_LAYER + BIASES_PER_LAYER)  // 2352

// Total floats per brain in GPU-layout flat array
#define BRAIN_WEIGHT_FLOATS (BRAIN_LAYERS * FLOATS_PER_LAYER)       // 7056

// Brain I/O sizes (padded to workgroup size)
#define BRAIN_INPUT_SIZE  NEURONS_PER_LAYER
#define BRAIN_OUTPUT_SIZE NEURONS_PER_LAYER

// How much the connection weight can vary (used by brain_mutate)
#define BRAIN_WEIGHT_RANGE 0.8f

// How much the bias can vary from init
#define BRAIN_BIAS_RANGE 1.0f

#endif
