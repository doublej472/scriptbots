#include "MLPBrain.h"
#include "helpers.h"
#include <stdlib.h>
#include <string.h>

void mlpbox_init(struct MLPBox *box) {
  memset(box, '\0', sizeof(struct MLPBox));

  for (int32_t i = 0; i < CONNS; i++) {
    box->w[i] = randf(-3.0f, 3.0f);
    // Make 30% of brain connect to input
    if (randf(0, 1) < 0.3f) {
      box->id[i] = randi(0, INPUTSIZE);
    } else {
      box->id[i] = randi(INPUTSIZE, BRAINSIZE);
    }
  }

  box->bias = randf(-1.5f, 1.5f);
  box->out = 0.0f;
}

void mlpbrain_init(struct MLPBrain *brain) {
  for (int32_t i = 0; i < BRAINSIZE; i++) {
    mlpbox_init(&brain->boxes[i]);
  }
}

void mlpbrain_init_other(struct MLPBrain *brain, const struct MLPBrain *other) {
  memcpy(&brain->boxes, &other->boxes, sizeof(struct MLPBox) * BRAINSIZE);
}

void mlpbrain_set(struct MLPBrain *target, const struct MLPBrain *source) {
  if (target != source) {
    memcpy(&target->boxes, &source->boxes, sizeof(struct MLPBox) * BRAINSIZE);
  }
}

void mlpbrain_tick(struct MLPBrain *brain, const float *in, float *out) {
  // do a single tick of the brain
  struct MLPBox *boxes = brain->boxes;

  // Mix in sensor data into the first few boxes
  for (size_t i = 0; i < INPUTSIZE; i++) {
    boxes[i].out = in[i];
  }

  // then do a dynamics tick and set all targets
  for (size_t i = INPUTSIZE; i < BRAINSIZE; i++) {
    struct MLPBox *abox = &boxes[i];

    float acc = 0;
    for (size_t j = 0; j < CONNS; j++) {
      uint32_t idx = abox->id[j];
      float val = boxes[idx].out;
      acc += val * abox->w[j];
    }

    acc += abox->bias;

    // activation function (clamped scaled linear)
    float scale = (CONNS+1.0f) / 3.0f;

    acc = fmax(fmin(scale, acc), 0.0f) / scale;

    abox->out = acc;
  }

  // finally set out[] to the last few boxes output
  for (int i = 0; i < OUTPUTSIZE; i++) {
    out[i] = boxes[BRAINSIZE - OUTPUTSIZE + i].out;
  }
}

/*
  Inputs:
  MR - how often do mutation occur?
  MR2 - how significant are the mutations?
*/
void mlpbrain_mutate(struct MLPBrain *brain, float mutaterate,
                     float mutaterate2) {
  struct MLPBox *boxes = brain->boxes;

  for (int32_t j = 0; j < BRAINSIZE; j++) {

    // Modify bias
    if (randf(0, 1) < mutaterate * 3) { // why 3?
      boxes[j].bias += randn(
          -mutaterate2,
          mutaterate2); // TODO: maybe make 0 be -mutaterate2 and add bottom
                        // limit?
                        //             a2.mutations.push_back("bias jiggled\n");
    }

    // Modify weight
    if (randf(0, 1) < mutaterate * 3) {
      int32_t rc = randi(0, CONNS);
      boxes[j].w[rc] += randn(-mutaterate2, mutaterate2);
      //          a2.mutations.push_back("weight jiggled\n");
    }

    // Modify connectivity of brain
    if (randf(0, 1) < mutaterate * 3) {
      for (int i = 0; i < randf(1, 16); i++) {
        int32_t rc = randi(0, CONNS);
        int32_t ri = randi(0, BRAINSIZE);
        boxes[j].id[rc] = ri;
      }
      //             a2.mutations.push_back("connectivity changed\n");
    }
  }
}

void mlpbrain_crossover(struct MLPBrain *target, const struct MLPBrain *source1,
                        const struct MLPBrain *source2) {

  for (size_t i = 0; i < BRAINSIZE; i++) {
    if (randf(0, 1) < 0.5) {
      target->boxes[i].bias = source1->boxes[i].bias;
      for (size_t j = 0; j < CONNS; j++) {
        target->boxes[i].id[j] = source1->boxes[i].id[j];
        target->boxes[i].w[j] = source1->boxes[i].w[j];
      }

    } else {
      target->boxes[i].bias = source2->boxes[i].bias;
      for (size_t j = 0; j < CONNS; j++) {
        target->boxes[i].id[j] = source2->boxes[i].id[j];
        target->boxes[i].w[j] = source2->boxes[i].w[j];
      }
    }
  }
}
