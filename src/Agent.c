#include "Agent.h"

#include "helpers.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Brain helpers (packed fp16, identical to GPU layout) ----

// Portable float32 ↔ float16 (no intrinsics)
static inline uint16_t float_to_half(float f) {
  uint32_t x;
  memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000u;
  int e = (int)((x >> 23) & 0xffu) - 127 + 15;
  uint32_t m = (x >> 13) & 0x3ffu;
  if (e <= 0)
    return (uint16_t)(sign | (m >> (1 - e)));
  if (e >= 31)
    return (uint16_t)(sign | 0x7c00u);
  return (uint16_t)(sign | ((uint32_t)e << 10) | m);
}

static inline float half_to_float(uint16_t h) {
  uint32_t sign = ((uint32_t)h >> 15) & 1u;
  uint32_t exp = ((uint32_t)h >> 10) & 0x1fu;
  uint32_t mant = (uint32_t)h & 0x3ffu;
  if (exp == 0) {
    if (mant == 0) {
      uint32_t r = sign << 31;
      float f;
      memcpy(&f, &r, 4);
      return f;
    }
    exp = 1;
    while ((mant & 0x400u) == 0) {
      mant <<= 1;
      exp--;
    }
    mant &= 0x3ffu;
  } else if (exp == 31) {
    uint32_t r = (sign << 31) | (0xffu << 23) | (mant << 13);
    float f;
    memcpy(&f, &r, 4);
    return f;
  }
  uint32_t r = (sign << 31) | (((exp - 15 + 127) & 0xffu) << 23) | (mant << 13);
  float f;
  memcpy(&f, &r, 4);
  return f;
}

void brain_init_random(uint32_t *w) {
  for (int i = 0; i < BRAIN_WEIGHT_UINTS; i++) {
    uint64_t r = genRandLong();
    double v = (double)(r >> 11) * (1.0 / 4503599627370496.0);
    float f0 = (float)((v - 1.0) * BRAIN_WEIGHT_RANGE);
    r = genRandLong();
    v = (double)(r >> 11) * (1.0 / 4503599627370496.0);
    float f1 = (float)((v - 1.0) * BRAIN_WEIGHT_RANGE);
    w[i] = ((uint32_t)float_to_half(f1) << 16) | float_to_half(f0);
  }
}

void brain_mutate(uint32_t *w, float rate, float mag) {
  for (int i = 0; i < BRAIN_WEIGHT_UINTS; i++) {
    uint16_t lo = (uint16_t)(w[i] & 0xffffu);
    uint16_t hi = (uint16_t)(w[i] >> 16);
    float flo = half_to_float(lo);
    float fhi = half_to_float(hi);
    bool ch_lo = false, ch_hi = false;
    if (randf(0.0f, 1.0f) < rate) {
      flo += (randf(0.0f, 1.0f) - 0.5f) * mag * 2.0f;
      if (flo > BRAIN_WEIGHT_RANGE)
        flo = BRAIN_WEIGHT_RANGE;
      if (flo < -BRAIN_WEIGHT_RANGE)
        flo = -BRAIN_WEIGHT_RANGE;
      ch_lo = true;
    }
    if (randf(0.0f, 1.0f) < rate) {
      fhi += (randf(0.0f, 1.0f) - 0.5f) * mag * 2.0f;
      if (fhi > BRAIN_WEIGHT_RANGE)
        fhi = BRAIN_WEIGHT_RANGE;
      if (fhi < -BRAIN_WEIGHT_RANGE)
        fhi = -BRAIN_WEIGHT_RANGE;
      ch_hi = true;
    }
    if (ch_lo || ch_hi)
      w[i] = ((uint32_t)(ch_hi ? float_to_half(fhi) : hi) << 16) | (ch_lo ? float_to_half(flo) : lo);
  }
}

// ---- Agent lifecycle ----

void agent_init(struct Agent *agent) {
  vector2f_init(&agent->pos, randf(0, WIDTH), randf(0, HEIGHT));
  agent->angle = randf(-M_PI, M_PI);
  agent->health = 1.0f + randf(0, 0.1f);
  agent->pending_damage = 0.0f;
  agent->pending_spiked = 0;
  agent->touch = 0;
  agent->age = 0;
  agent->spikeLength = 0;
  agent->red = 0;
  agent->gre = 0;
  agent->blu = 0;
  agent->w1 = 0;
  agent->w2 = 0;
  agent->soundmul = 1;
  agent->give = 0;
  agent->clockf1 = randf(5, 100);
  agent->clockf2 = randf(5, 100);
  agent->boost = 0;
  agent->indicator = 0;
  agent->gencount = 0;
  agent->ir = 0;
  agent->ig = 0;
  agent->ib = 0;
  agent->hybrid = 0;
  agent->herbivore = randf(0, 1);
  agent->rep = 0;
  agent->repcounter = agent->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
                      (1.0f - agent->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
  agent->numchildren = 0;
  agent->MUTRATE1 = METAMUTRATE1;
  agent->MUTRATE2 = METAMUTRATE2;
  agent->spiked = 0;
  agent->brain_chunk = ~0u;
  agent->brain_index = 0;

  agent->spike_outbox_count = 0;
  agent->food_request = 0.0f;
  agent->pending_health_delta = 0.0f;

  brain_init_random(agent->brain);
}

void agent_print(struct Agent *agent) { printf("Agent age=%i\n", agent->age); }

void agent_initevent(struct Agent *agent, float size, float r, float g, float b) {
  if (size > agent->indicator) {
    agent->indicator = size;
    agent->ir = r;
    agent->ig = g;
    agent->ib = b;
  }
}

void agent_reproduce(struct Agent *child, struct Agent *parent) {
  struct Vector2f fb;
  vector2f_init(&fb, randf(BOTRADIUS, BOTRADIUS * 3.0f), 0.0f);
  vector2f_rotate(&fb, -child->angle);
  vector2f_add(&child->pos, &parent->pos, &fb);

  parent->numchildren++;
  child->gencount = parent->gencount + 1;
  child->repcounter = child->herbivore * randf(REPRATEH - 0.2f, REPRATEH + 0.2f) +
                      (1.0f - child->herbivore) * randf(REPRATEC - 0.2f, REPRATEC + 0.2f);

  child->MUTRATE1 = parent->MUTRATE1;
  child->MUTRATE2 = parent->MUTRATE2;
  if (randf(0, 1) < 0.2f)
    child->MUTRATE1 = cap(randn(parent->MUTRATE1, METAMUTRATE1));
  if (randf(0, 1) < 0.2f)
    child->MUTRATE2 = randn(parent->MUTRATE2, METAMUTRATE2);
  if (child->MUTRATE1 < 0.02f)
    child->MUTRATE1 = 0.02f;
  if (child->MUTRATE2 < 0.02f)
    child->MUTRATE2 = 0.02f;
  child->herbivore = cap(randn(parent->herbivore, 0.03f));
  if (randf(0, 1) < child->MUTRATE1 * 5.0f)
    child->clockf1 = randn(child->clockf1, child->MUTRATE2);
  if (child->clockf1 < 2.0f)
    child->clockf1 = 2.0f;
  if (randf(0, 1) < child->MUTRATE1 * 5.0f)
    child->clockf2 = randn(child->clockf2, child->MUTRATE2);
  if (child->clockf2 < 2.0f)
    child->clockf2 = 2.0f;

  // Mutate brain (packed fp16, identical to GPU)
  memcpy(child->brain, parent->brain, BRAIN_WEIGHT_UINTS * sizeof(uint32_t));
  brain_mutate(child->brain, child->MUTRATE1, child->MUTRATE2);
}

void agent_process_health(struct Agent *agent) {
  float healthloss = LOSS_BASE;
  if (agent->age > 500.0f)
    healthloss += (LOSS_AGE * ((agent->age - 500.0f) / 250.0f));

  if (agent->boost) {
    healthloss += LOSS_SPEED * BOTSPEED * ((fabsf(agent->w1) + fabsf(agent->w2)) / 2.0f) + LOSS_BOOST * agent->boost;
  } else {
    healthloss += LOSS_SPEED * BOTSPEED * (fabsf(agent->w1) + fabsf(agent->w2));
  }
  healthloss += LOSS_SHOUTING * agent->soundmul;
  agent->health -= healthloss;
}
