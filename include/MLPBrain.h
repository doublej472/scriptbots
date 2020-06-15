#ifndef MLPBRAIN_H
#define MLPBRAIN_H
#include <stdint.h>

#include "helpers.h"
#include "settings.h"

struct MLPBox {
  float w[CONNS]; // weight of each connecting box
  int32_t id[CONNS];  // id in boxes[] of the connecting box
  float kp;             // damper
  float gw;             // global w
  float bias;

  // state variables
  float target; // target value this node is going toward
  float out;    // current output
};

void mlpbox_init(struct MLPBox *box);

/**
 * Damped Weighted Recurrent AND/OR Network
 */
struct MLPBrain {
  struct MLPBox boxes[BRAINSIZE];
};

void mlpbrain_init(struct MLPBrain *brain);
void mlpbrain_init_other(struct MLPBrain *brain, const struct MLPBrain *other);
void mlpbrain_set(struct MLPBrain *target, const struct MLPBrain *source);
void mlpbrain_tick(struct MLPBrain *brain, const float *in, float *out);
void mlpbrain_mutate(struct MLPBrain *brain, float mutaterate, float mutaterate2);
void mlpbrain_crossover(struct MLPBrain *target, const struct MLPBrain *source1, const
  struct MLPBrain *source2);

#endif
