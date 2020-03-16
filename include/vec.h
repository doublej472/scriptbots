#ifndef VEC_H
#define VEC_H

#include "include/Agent.h"

struct AVec {
  Agent *agents;
  size_t size;
  size_t allocated;
};

void avec_init(AVec* vec, size_t size);
void avec_free(AVec* vec);
void avec_delete(AVec* vec, size_t idx);
void avec_push_back(AVec* vec, Agent& a);
Agent& avec_get(AVec* vec, size_t idx);

#endif // VEC_H
