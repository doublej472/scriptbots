#include "helpers.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if defined(WIN32) && !defined(UNIX)
#include <sysinfoapi.h>
#endif

void init_thread_random() { srand(time(0)); }

// uniform random in [a,b)
inline float randf(float a, float b) {
  return ((b - a) * ((float)rand() / (float)RAND_MAX)) + a;
}

// uniform random int32_t in [a,b)
inline int32_t randi(int32_t a, int32_t b) {
  return (rand() % (b - a)) + a;
}

// normalvariate random N(mu, sigma)
float randn(float mu, float sigma) {
  static int32_t deviateAvailable = 0; //	flag
  static float storedDeviate;          //	deviate from previous calculation
  float polar, rsquared, var1, var2;
  if (!deviateAvailable) {
    do {
      var1 = 2.0f * (((float)rand()) / ((float)(RAND_MAX))) - 1.0f;
      var2 = 2.0f * (((float)rand()) / ((float)(RAND_MAX))) - 1.0f;
      rsquared = var1 * var1 + var2 * var2;
    } while (rsquared >= 1.0f || rsquared == 0.0f);
    polar = sqrtf(-2.0f * logf(rsquared) / rsquared);
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

// inline float fast_exp(float a) {
//   union {
//     float f;
//     int x;
//   } u;
//   u.x = (int)(12102203 * a + 1064866805);
//   return u.f;
// }

// Get number of processors in the system
inline long get_nprocs() {
#if defined(WIN32) && !defined(UNIX)
SYSTEM_INFO sysinfo;
GetSystemInfo(&sysinfo);
return sysinfo.dwNumberOfProcessors;
#elif defined(UNIX) && !defined(WIN32)
  return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

void *alloc_aligned(size_t alignment, size_t size) {
#if defined(WIN32) && !defined(UNIX)
return _aligned_malloc(size, alignment);
#elif defined(UNIX) && !defined(WIN32)
return aligned_alloc(alignment, size);
#else
#error "No aligned alloc available!!!"
#endif
}

void free_brain(void *f) {
#if defined(WIN32) && !defined(UNIX)
_aligned_free(f);
#elif defined(UNIX) && !defined(WIN32)
free(f);
#else
#error "No aligned free available!!!"
#endif
}
