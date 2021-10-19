#include "include/Agent.h"
#include "include/vec.h"

#include <stdlib.h>

void avec_init(struct AVec* vec, size_t size) {
  vec->agents = (struct Agent*) malloc(sizeof(struct Agent) * size);
  vec->allocated = size;
  vec->size = 0;
}

void avec_free(struct AVec* vec) {
  for (size_t i = 0; i < vec->size; i++) {
    free(avec_get(vec, i)->close_agents);
  }
  free(vec->agents);
  vec->allocated = 0;
  vec->size = 0;
}

// Very simple deletion, where the last agent is moved
// to the deleted agent, and the vec is shrunk by 1
void avec_delete(struct AVec *vec, size_t idx) {
  free(avec_get(vec, idx)->close_agents);
  vec->agents[idx] = vec->agents[vec->size-1];
  vec->size--;

  if (vec->size < vec->allocated - 16) {
    avec_shrink(vec, vec->size);
  }
}

void avec_push_back(struct AVec *vec, struct Agent a) {
  //printf("vec->size=%ld, vec->allocated=%ld\n", vec->size, vec->allocated);
  if (vec->size == vec->allocated) {
    avec_shrink(vec, vec->allocated + 16);
  }
  vec->agents[vec->size++] = a;
}

// Shrinks the array to size or vec->size, whichever is greater
void avec_shrink(struct AVec *vec, size_t size) {
  int newsize = size > vec->size ? size : vec->size;

  vec->agents = (struct Agent*) realloc(vec->agents, sizeof(struct Agent) * newsize);
  vec->allocated = newsize;
}

struct Agent* avec_get(struct AVec *vec, size_t idx) {
  return &vec->agents[idx];
}
