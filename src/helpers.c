#include "include/helpers.h"
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

// uniform random in [a,b)
inline float randf(float a, float b) {
  return ((b - a) * ((float)rand() / (float)RAND_MAX)) + a;
}

// uniform random int32_t in [a,b)
inline int32_t randi(int32_t a, int32_t b) { return (rand() % (b - a)) + a; }

// normalvariate random N(mu, sigma)
double randn(double mu, double sigma) {
  static int32_t deviateAvailable = 0; //	flag
  static float storedDeviate;           //	deviate from previous calculation
  double polar, rsquared, var1, var2;
  if (!deviateAvailable) {
    do {
      var1 = 2.0 * ( ((double) rand()) / ((double) (RAND_MAX))) - 1.0;
      var2 = 2.0 * ( ((double) rand()) / ((double) (RAND_MAX))) - 1.0;
      rsquared = var1 * var1 + var2 * var2;
    } while (rsquared >= 1.0 || rsquared == 0.0);
    polar = sqrt(-2.0 * log(rsquared) / rsquared);
    storedDeviate = var1 * polar;
    deviateAvailable = 1;
    return var2 * polar * sigma + mu;
  } else {
    deviateAvailable = 0;
    return storedDeviate * sigma + mu;
  }
}

// cap value between 0 and 1
inline float cap(float a) {
  if (a < 0)
    return 0;
  if (a > 1)
    return 1;
  return a;
}

// inline float fast_exp_iter(float x) {
//   for (int i = 0; i < 8; i++) {
//     x *= x;
//   }
//   return x;
// }

// // Fast exp(), not accurate
// inline float fast_exp(float x) {
//   x = fast_exp_iter(1.0f + x / 1024);
  
//   return x;
// }

// https://www.musicdsp.org/en/latest/Other/222-fast-exp-approximations.html
inline float fast_exp(float x) {
    return (24+x*(24+x*(12+x*(4+x))))*0.041666666f;
}

// Get number of processors in the system
inline long get_nprocs() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}
