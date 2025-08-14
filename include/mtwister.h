#ifndef __MTWISTER_H
#define __MTWISTER_H

#include <stdint.h>

void seedRand(uint64_t seed);
uint64_t genRandLong();
float genRand();

#endif /* #ifndef __MTWISTER_H */
