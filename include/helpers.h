#pragma once
#include <stddef.h>
#include <stdint.h>

void init_thread_random();

// Inlined hot-path functions (cross-TU inlinable)

#include "mtwister.h" // genRand()
#include <assert.h>
#include <math.h>

static inline float approx_atan2(float y, float x) {
  const float ONEQTR_PI = (float)M_PI / 4.0f;
  const float THRQTR_PI = 3.0f * (float)M_PI / 4.0f;
  float r, angle;
  float abs_y = fabsf(y) + 1e-10f;
  if (x < 0.0f) {
    r = (x + abs_y) / (abs_y - x);
    angle = THRQTR_PI;
  } else {
    r = (x - abs_y) / (x + abs_y);
    angle = ONEQTR_PI;
  }
  angle += (0.1963f * r * r - 0.9817f) * r;
  if (y < 0.0f)
    return -angle;
  return angle;
}

// uniform random in [a,b)
static inline float randf(float a, float b) { return (b - a) * genRand() + a; }

// uniform random int32_t in [a,b)
static inline int32_t randi(int32_t a, int32_t b) {
  assert(b >= a);
  if (b <= a)
    return a;
  return (int32_t)(genRandLong() % (uint64_t)(b - a)) + a;
}

// normalvariate random N(mu, sigma)
float randn(float mu, float sigma);

// cap value between 0 and 1
static inline float cap(float a) {
  if (a < 0)
    return 0;
  if (a > 1)
    return 1;
  return a;
}

// Get number of processors in the system
long get_nprocs();

// ---- Frame timing helpers ----
#include <time.h>

// Capture current monotonic time into *t ("start the timer")
static inline void timer_reset(struct timespec *t) { clock_gettime(CLOCK_MONOTONIC, t); }

// Return elapsed milliseconds since timer_reset(t). Does NOT modify t —
// safe to call multiple times for multiple phase boundaries from one origin.
static inline double timer_since_ms(const struct timespec *t) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - t->tv_sec) * 1000.0 + (now.tv_nsec - t->tv_nsec) / 1000000.0;
}

// Return elapsed ms since timer_reset(t) AND reset t to now.
// Convenient for single-point timing; prefer timer_since_ms for multi-phase.
static inline double timer_elapsed_ms(struct timespec *t) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double ms = (now.tv_sec - t->tv_sec) * 1000.0 + (now.tv_nsec - t->tv_nsec) / 1000000.0;
  *t = now;
  return ms;
}
