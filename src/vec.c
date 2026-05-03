#include "vec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void avec_init(struct AVec *vec, size_t size) {
  vec->size = 0;
  vec->allocated = size;
  vec->agents = malloc(size * sizeof(struct Agent *));
}

void avec_free(struct AVec *vec) { free(vec->agents); }

void avec_delete(struct AVec *vec, size_t idx) {
  if (vec->size == 0) return;
  vec->size--;
  vec->agents[idx] = vec->agents[vec->size];
}

void avec_push_back(struct AVec *vec, struct Agent *a) {
  if (vec->size >= vec->allocated) {
    vec->allocated *= 2;
    struct Agent **p = realloc(vec->agents, vec->allocated * sizeof(struct Agent *));
    if (!p) { fprintf(stderr, "FATAL: out of memory\n"); return; }
    vec->agents = p;
  }
  vec->agents[vec->size++] = a;
}

void avec_shrink(struct AVec *vec, size_t size) { vec->size = size; }

struct Agent *avec_get(struct AVec *vec, size_t idx) { return vec->agents[idx]; }
