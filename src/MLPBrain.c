#include <string.h>
#include <stdlib.h>
#include "include/helpers.h"
#include "include/MLPBrain.h"

void mlpbox_init(struct MLPBox *box) {
  memset(box, '\0', sizeof(struct MLPBox));

  for (int i = 0; i < CONNS; i++) {
    box->w[i] = randf(-3, 3);
    // Make 30% of brain connect to input
    if (randf(0, 1) < 0.3) {
      box->id[i] = randi(0, INPUTSIZE);
    } else {
      box->id[i] = randi(INPUTSIZE, BRAINSIZE);
    }
  }

  // how fast neuron/box moves towards its target. 1 is instant.
  box->kp = randf(0.1,1);
  box->gw = randf(0, 5);
  box->bias = randf(-1.5, 1.5);
  box->out = 0;
  box->target = 0;
}

void mlpbrain_init(struct MLPBrain *brain) {
  for (int i = 0; i < BRAINSIZE; i++) {
    mlpbox_init(&brain->boxes[i]);
  }
}

void mlpbrain_init_other(struct MLPBrain *brain, const struct MLPBrain *other) {
  memcpy(brain->boxes, other->boxes, sizeof(struct MLPBox) * BRAINSIZE);
}

void mlpbrain_set(struct MLPBrain *target, const struct MLPBrain *source) {
  if (target != source) {
    memcpy(target->boxes, source->boxes, sizeof(struct MLPBox) * BRAINSIZE);
  }
}

void mlpbrain_tick(struct MLPBrain *brain, const float *in, float *out) {
  // do a single tick of the brain
  struct MLPBox* boxes = brain->boxes;

  // take first few boxes and set their out to in[].
  for (int i = 0; i < INPUTSIZE; i++) {
    boxes[i].out = in[i];
  }

  // then do a dynamics tick and set all targets
  for (int i = INPUTSIZE; i < BRAINSIZE; i++) {
    struct MLPBox* abox = &boxes[i];

    float acc = 0;
    for (int j = 0; j < CONNS; j++) {
      int idx = abox->id[j];
      float val = boxes[idx].out;
      acc += val * abox->w[j];
    }
    acc *= abox->gw;
    acc += abox->bias;

    // put through sigmoid
    acc = 1.0 / (1.0 + fast_exp(-acc)); // logistic function, ranges from 0 to 1

    abox->target = acc;
  }

  // make all boxes go a bit toward target
  for (int i = INPUTSIZE; i < BRAINSIZE; i++) {
    struct MLPBox *abox = &boxes[i];
    abox->out = abox->out + (abox->target - abox->out) * abox->kp;
  }

  // finally set out[] to the last few boxes output
  for (int i = 0; i < OUTPUTSIZE; i++) {
    out[i] = boxes[BRAINSIZE - 1 - i].out;
  }
}

/*
  Inputs:
  MR - how often do mutation occur?
  MR2 - how significant are the mutations?
*/
void mlpbrain_mutate(struct MLPBrain *brain, float mutaterate, float mutaterate2) {
  struct MLPBox* boxes = brain->boxes;

  for (int j = 0; j < BRAINSIZE; j++) {

    // Modify bias
    if (randf(0, 1) < mutaterate * 3) { // why 3?
      boxes[j].bias += randn(
          0, mutaterate2); // TODO: maybe make 0 be -mutaterate2 and add bottom limit?
                   //             a2.mutations.push_back("bias jiggled\n");
    }

    // Modify kp
    if (randf(0, 1) < mutaterate * 3) {
      boxes[j].kp += randn(0, mutaterate2); // TODO: maybe make the 0 be -mutaterate2 ?

      // Limit the change:
      if (boxes[j].kp < 0.01)
        boxes[j].kp = 0.01;
      if (boxes[j].kp > 1)
        boxes[j].kp = 1;

      //             a2.mutations.push_back("kp jiggled\n");
    }

    // Modify gw
    if (randf(0, 1) < mutaterate * 3) {
      boxes[j].gw += randn(0, mutaterate2);
      if (boxes[j].gw < 0)
        boxes[j].gw = 0;

      //             a2.mutations.push_back("kp jiggled\n");
    }

    // Modify weight
    if (randf(0, 1) < mutaterate * 3) {
      int rc = randi(0, CONNS);
      boxes[j].w[rc] += randn(0, mutaterate2);
      //          a2.mutations.push_back("weight jiggled\n");
    }

    // Modify connectivity of brain
    if (randf(0, 1) < mutaterate * 3) {
      int rc = randi(0, CONNS);
      int ri = randi(0, BRAINSIZE);
      boxes[j].id[rc] = ri;
      //             a2.mutations.push_back("connectivity changed\n");
    }
  }
}

void mlpbrain_crossover(
  struct MLPBrain *target,
  const struct MLPBrain *source1,
  const struct MLPBrain *source2) {

  for (size_t i = 0; i < BRAINSIZE; i++) {
    if (randf(0, 1) < 0.5) {
      target->boxes[i].bias = source1->boxes[i].bias;
      target->boxes[i].gw = source1->boxes[i].gw;
      target->boxes[i].kp = source1->boxes[i].kp;
      for (size_t j = 0; j < CONNS; j++) {
        target->boxes[i].id[j] = source1->boxes[i].id[j];
        target->boxes[i].w[j] = source1->boxes[i].w[j];
      }

    } else {
      target->boxes[i].bias = source2->boxes[i].bias;
      target->boxes[i].gw = source2->boxes[i].gw;
      target->boxes[i].kp = source2->boxes[i].kp;
      for (size_t j = 0; j < CONNS; j++) {
        target->boxes[i].id[j] = source2->boxes[i].id[j];
        target->boxes[i].w[j] = source2->boxes[i].w[j];
      }
    }
  }
}
