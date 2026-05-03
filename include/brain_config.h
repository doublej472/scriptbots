// brain_config.h — neural network topology and GPU-compatible layout constants
// Included by both C runtime and GLSL shaders (via glslc -I).
#ifndef BRAIN_CONFIG_H
#define BRAIN_CONFIG_H

// How many weight-matrix transitions (hidden layers + output layer)
#define BRAIN_LAYERS 4

// How many neurons per layer (also GPU workgroup size, must be ≤ maxComputeWorkGroupInvocations)
#define NEURONS_PER_LAYER 64

// How many floats per layer in GPU layout: weights[N×N] + biases[N]
#define WEIGHTS_PER_LAYER  (NEURONS_PER_LAYER * NEURONS_PER_LAYER)
#define BIASES_PER_LAYER    NEURONS_PER_LAYER
#define FLOATS_PER_LAYER    (WEIGHTS_PER_LAYER + BIASES_PER_LAYER)

// Total floats per brain in GPU-layout flat array
#define BRAIN_WEIGHT_FLOATS (BRAIN_LAYERS * FLOATS_PER_LAYER)

// Brain I/O sizes (padded to workgroup size)
#define BRAIN_INPUT_SIZE  NEURONS_PER_LAYER
#define BRAIN_OUTPUT_SIZE NEURONS_PER_LAYER

// How much the connection weight can vary (used by brain_mutate)
#define BRAIN_WEIGHT_RANGE 0.8f

// How much the bias can vary from init
#define BRAIN_BIAS_RANGE 1.0f

#endif
