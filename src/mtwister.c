/* An implementation of the MT19937 Algorithm for the Mersenne Twister
 * by Evan Sultanik.  Based upon the pseudocode in: M. Matsumoto and
 * T. Nishimura, "Mersenne Twister: A 623-dimensionally
 * equidistributed uniform pseudorandom number generator," ACM
 * Transactions on Modeling and Computer Simulation Vol. 8, No. 1,
 * January pp.3-30 1998.
 *
 * http://www.sultanik.com/Mersenne_twister
 */

#define UPPER_MASK 0x80000000
#define LOWER_MASK 0x7fffffff
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000

#include "mtwister.h"

thread_local MTRand mtrand;

inline static void m_seedRand(unsigned long seed) {
  /* set initial seeds to mt[STATE_VECTOR_LENGTH] using the generator
   * from Line 25 of Table 1 in: Donald Knuth, "The Art of Computer
   * Programming," Vol. 2 (2nd Ed.) pp.102.
   */
  mtrand.mt[0] = seed & 0xffffffff;
  for (mtrand.index = 1; mtrand.index < STATE_VECTOR_LENGTH; mtrand.index++) {
    mtrand.mt[mtrand.index] = (6069 * mtrand.mt[mtrand.index - 1]) & 0xffffffff;
  }
}

/**
 * Creates a new random number generator from a given seed.
 */
void seedRand(unsigned long seed) { m_seedRand(seed); }

/**
 * Generates a pseudo-randomly generated long.
 */
unsigned long genRandLong() {

  unsigned long y;
  static unsigned long mag[2] = {
      0x0, 0x9908b0df}; /* mag[x] = x * 0x9908b0df for x = 0,1 */
  if (mtrand.index >= STATE_VECTOR_LENGTH || mtrand.index < 0) {
    /* generate STATE_VECTOR_LENGTH words at a time */
    int kk;
    if (mtrand.index >= STATE_VECTOR_LENGTH + 1 || mtrand.index < 0) {
      m_seedRand(4357);
    }
    for (kk = 0; kk < STATE_VECTOR_LENGTH - STATE_VECTOR_M; kk++) {
      y = (mtrand.mt[kk] & UPPER_MASK) | (mtrand.mt[kk + 1] & LOWER_MASK);
      mtrand.mt[kk] = mtrand.mt[kk + STATE_VECTOR_M] ^ (y >> 1) ^ mag[y & 0x1];
    }
    for (; kk < STATE_VECTOR_LENGTH - 1; kk++) {
      y = (mtrand.mt[kk] & UPPER_MASK) | (mtrand.mt[kk + 1] & LOWER_MASK);
      mtrand.mt[kk] = mtrand.mt[kk + (STATE_VECTOR_M - STATE_VECTOR_LENGTH)] ^
                      (y >> 1) ^ mag[y & 0x1];
    }
    y = (mtrand.mt[STATE_VECTOR_LENGTH - 1] & UPPER_MASK) |
        (mtrand.mt[0] & LOWER_MASK);
    mtrand.mt[STATE_VECTOR_LENGTH - 1] =
        mtrand.mt[STATE_VECTOR_M - 1] ^ (y >> 1) ^ mag[y & 0x1];
    mtrand.index = 0;
  }
  y = mtrand.mt[mtrand.index++];
  y ^= (y >> 11);
  y ^= (y << 7) & TEMPERING_MASK_B;
  y ^= (y << 15) & TEMPERING_MASK_C;
  y ^= (y >> 18);
  return y;
}

/**
 * Generates a pseudo-randomly generated float in the range [0..1].
 */
float genRand() { return ((float)genRandLong() / (unsigned long)0xffffffff); }
