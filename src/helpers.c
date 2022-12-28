#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <sysinfoapi.h>
#endif

#include "helpers.h"

void init_thread_random() { srand(time(0)); }

inline float approx_atan2(float y, float x) {
  //http://pubs.opengroup.org/onlinepubs/009695399/functions/atan2.html
  //Volkan SALMA

  const float ONEQTR_PI = M_PI / 4.0;
	const float THRQTR_PI = 3.0 * M_PI / 4.0;
	float r, angle;
	float abs_y = fabs(y) + 1e-10f;      // kludge to prevent 0/0 condition
	if ( x < 0.0f )
	{
		r = (x + abs_y) / (abs_y - x);
		angle = THRQTR_PI;
	}
	else
	{
		r = (x - abs_y) / (x + abs_y);
		angle = ONEQTR_PI;
	}
	angle += (0.1963f * r * r - 0.9817f) * r;
	if ( y < 0.0f )
		return( -angle );     // negate if in quad III or IV
	else
		return( angle );
}

// uniform random in [a,b)
float randf(float a, float b) {
  return ((b - a) * ((float)rand() / (float)RAND_MAX)) + a;
}

// uniform random int32_t in [a,b)
inline int32_t randi(int32_t a, int32_t b) { return (rand() % (b - a)) + a; }

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
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
#elif __linux__
  return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

void *alloc_aligned(size_t alignment, size_t size) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  return _aligned_malloc(size, alignment);
#elif __linux__
  return aligned_alloc(alignment, size);
#else
#error "No aligned alloc available!!!"
#endif
}

void free_brain(void *f) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  _aligned_free(f);
#elif __linux__
  free(f);
#else
#error "No aligned free available!!!"
#endif
}
