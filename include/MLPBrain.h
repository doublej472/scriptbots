#ifndef MLPBRAIN_H
#define MLPBRAIN_H

#include "boost.h"
#include "helpers.h"
#include "settings.h"

#include <vector>

class MLPBox {
  // Serialization ------------------------------------------
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    // Add all class variables here:
    ar &w;
    ar &id;
    ar &kp;
    ar &bias;
    ar &target;
    ar &out;
  }
  // ---------------------------------------------------------
public:
  MLPBox();
  ~MLPBox();

  float w[CONNS]; // weight of each connecting box
  int id[CONNS];  // id in boxes[] of the connecting box
  float kp;             // damper
  float gw;             // global w
  float bias;

  // state variables
  float target; // target value this node is going toward
  float out;    // current output
};

/**
 * Damped Weighted Recurrent AND/OR Network
 */
class MLPBrain {
  // Serialization ------------------------------------------
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    // Add all class variables here:
    ar &boxes;
  }
  // ---------------------------------------------------------
public:
  MLPBox boxes[BRAINSIZE];

  MLPBrain();
  virtual ~MLPBrain();
  MLPBrain(const MLPBrain &other);
  virtual MLPBrain &operator=(const MLPBrain &other);

  void tick(std::vector<float> &in, std::vector<float> &out);
  void mutate(float MR, float MR2);
  MLPBrain crossover(const MLPBrain &other);

private:
  void init();
};

#endif
