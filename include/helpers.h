#ifndef HELPERS_H
#define HELPERS_H
#include <stdint.h>

// uniform random in [a,b)
float randf(float a, float b);

// uniform random int32_t in [a,b)
int32_t randi(int32_t a, int32_t b);

// normalvariate random N(mu, sigma)
double randn(double mu, double sigma);

// cap value between 0 and 1
float cap(float a);

// Fast exp(), not accurate
float fast_exp(float x);

// Get number of processors in the system
long get_nprocs();
#endif
