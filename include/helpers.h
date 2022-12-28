#ifndef HELPERS_H
#define HELPERS_H
#include <stdint.h>
#include <stddef.h>

void init_thread_random();

float approx_atan2(float y, float x);

// uniform random in [a,b)
float randf(float a, float b);

// uniform random int32_t in [a,b)
int32_t randi(int32_t a, int32_t b);

// normalvariate random N(mu, sigma)
float randn(float mu, float sigma);

// cap value between 0 and 1
float cap(float a);

// Get number of processors in the system
long get_nprocs();

void *alloc_aligned(size_t alignment, size_t size);
void free_brain(void *f);
#endif
