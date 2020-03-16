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
class MLPBrain {
public:
  MLPBox boxes[BRAINSIZE];

  MLPBrain();
  virtual ~MLPBrain();
  MLPBrain(const MLPBrain &other);
  virtual MLPBrain &operator=(const MLPBrain &other);

  void tick(const float *in, float *out);
  void mutate(float MR, float MR2);
  MLPBrain crossover(const MLPBrain &other);

private:
  void init();
};

#endif
