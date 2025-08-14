/* Stolen shamelessly from
 * https://lemire.me/blog/2019/03/19/the-fastest-conventional-random-number-generator-that-can-pass-big-crush/
 */

#include <limits.h>
#include <stdint.h>

#include "mtwister.h"

#ifndef thread_local
#if __STDC_VERSION__ >= 201112 && !defined __STDC_NO_THREADS__
#define thread_local _Thread_local
#elif defined _WIN32 && (defined _MSC_VER || defined __ICL ||                  \
                         defined __DMC__ || defined __BORLANDC__)
#define thread_local __declspec(thread)
/* note that ICC (linux) and Clang are covered by __GNUC__ */
#elif defined __GNUC__ || defined __SUNPRO_C || defined __xlC__
#define thread_local __thread
#else
#error "Cannot define thread_local"
#endif
#endif

thread_local __uint128_t g_lehmer64_state;

static uint64_t lehmer64() {
  g_lehmer64_state *= 0xda942042e4dd58b5;
  return g_lehmer64_state >> 64;
}

/**
 * Creates a new random number generator from a given seed.
 */
void seedRand(uint64_t seed) { g_lehmer64_state = seed; }

/**
 * Generates a pseudo-randomly generated long.
 */
uint64_t genRandLong() { return lehmer64(); }

/**
 * Generates a pseudo-randomly generated float in the range [0..1].
 */
float genRand() {
  uint64_t rand_long = genRandLong();
  float ratio = ((float)rand_long) / ((float)UINT64_MAX);
  return ratio;
}
