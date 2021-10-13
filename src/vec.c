#include "include/Agent.h"
#include "include/vec.h"

#include <stdlib.h>

void avec_init(struct AVec* vec, size_t size) {
  vec->agents = (struct Agent*) malloc(sizeof(struct Agent) * size);
  vec->allocated = size;
  vec->size = 0;
}

void avec_free(struct AVec* vec) {
  free(vec->agents);
}

// Very simple deletion, where the last agent is moved
// to the deleted agent, and the vec is shrunk by 1
void avec_delete(struct AVec *vec, size_t idx) {
  vec->agents[idx] = vec->agents[vec->size-1];
  vec->size--;
}

void avec_push_back(struct AVec *vec, struct Agent a) {
  if (vec->size == vec->allocated) {
    vec->agents = (struct Agent*) realloc(vec->agents, sizeof(struct Agent) * vec->allocated*2);
    vec->allocated = vec->allocated + 16;
  }
  vec->agents[vec->size] = a;
  vec->size++;
}

struct Agent* avec_get(struct AVec *vec, size_t idx) {
  return &vec->agents[idx];
}
