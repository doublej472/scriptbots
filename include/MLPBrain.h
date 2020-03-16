#ifndef MLPBRAIN_H
#define MLPBRAIN_H

#include "helpers.h"
#include "settings.h"

struct MLPBox {
  float w[CONNS]; // weight of each connecting box
  int id[CONNS];  // id in boxes[] of the connecting box
  float kp;             // damper
  float gw;             // global w
  float bias;

  // state variables
  float target; // target value this node is going toward
  float out;    // current output
};

void mlpbox_init(MLPBox& box);

/**
 * Damped Weighted Recurrent AND/OR Network
 */
struct MLPBrain {
  MLPBox boxes[BRAINSIZE];
};

void mlpbrain_init(MLPBrain& brain);
void mlpbrain_init(MLPBrain& brain, const MLPBrain& other);
void mlpbrain_set(MLPBrain& target, const MLPBrain& source);
void mlpbrain_tick(MLPBrain& brain, const float *in, float *out);
void mlpbrain_mutate(MLPBrain& brain, float mutaterate, float mutaterate2);
void mlpbrain_crossover( MLPBrain& target, const MLPBrain& source1, const
  MLPBrain& source2);

#endif
