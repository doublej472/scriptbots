/* xoshiro256++ 1.0 — David Blackman and Sebastiano Vigna (vigna@acm.org)
   Public domain.  Passes BigCrush.  256-bit state, period 2^256−1.
   Adapted as a drop-in replacement for mtwister.h. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Per-thread state (thread_local)
#ifndef thread_local
#if __STDC_VERSION__ >= 201112 && !defined __STDC_NO_THREADS__
#define thread_local _Thread_local
#elif defined _WIN32 && (defined _MSC_VER || defined __ICL || defined __DMC__ || defined __BORLANDC__)
#define thread_local __declspec(thread)
#elif defined __GNUC__ || defined __SUNPRO_C || defined __xlC__
#define thread_local __thread
#else
#error "Cannot define thread_local"
#endif
#endif

typedef struct {
  uint64_t s[4];
} Xoshiro256ppState;

static thread_local Xoshiro256ppState tls_state;

static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t splitmix64(uint64_t *x) {
  uint64_t z = (*x += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

void seedRand(uint64_t seed) {
  uint64_t sm = seed;
  tls_state.s[0] = splitmix64(&sm);
  tls_state.s[1] = splitmix64(&sm);
  tls_state.s[2] = splitmix64(&sm);
  tls_state.s[3] = splitmix64(&sm);
}

static uint64_t xoshiro256pp(Xoshiro256ppState *state) {
  uint64_t *s = state->s;
  const uint64_t result = rotl(s[0] + s[3], 23) + s[0];
  const uint64_t t = s[1] << 17;

  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];

  s[2] ^= t;
  s[3] = rotl(s[3], 45);

  return result;
}

uint64_t genRandLong(void) { return xoshiro256pp(&tls_state); }

float genRand(void) {
  uint64_t r = genRandLong();
  return (float)(r >> 40) * (1.0f / 16777216.0f); // 24 bits of mantissa, no division
}

#ifdef __cplusplus
}
#endif
