#include "helpers.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <sysinfoapi.h>
#elif __linux__ || __APPLE__
#include <unistd.h>
#endif

void init_thread_random() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  long seed = ts.tv_nsec;
  seed ^= ts.tv_sec;
  seedRand(seed);
}

float randn(float mu, float sigma) {
  static int32_t deviateAvailable = 0;
  static float storedDeviate;
  float polar, rsquared, var1, var2;
  if (!deviateAvailable) {
    do {
      var1 = 2.0f * genRand() - 1.0f;
      var2 = 2.0f * genRand() - 1.0f;
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

long get_nprocs() {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
#elif __linux__ || __APPLE__
  return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}
