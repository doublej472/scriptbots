#ifndef HELPERS_H
#define HELPERS_H

// uniform random in [a,b)
float randf(float a, float b);

// uniform random int in [a,b)
int randi(int a, int b);

// normalvariate random N(mu, sigma)
double randn(double mu, double sigma);

// cap value between 0 and 1
float cap(float a);

// Fast exp(), not accurate
float fast_exp(float x);

// Get number of processors in the system
long get_nprocs();
#endif
