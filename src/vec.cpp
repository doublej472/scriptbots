#include "include/Agent.h"
#include "include/vec.h"

void avec_init(AVec* vec, size_t size) {
  vec->agents = (Agent*) malloc(sizeof(Agent) * size);
  vec->allocated = size;
  vec->size = 0;
}

void avec_free(AVec* vec) {
  free(vec->agents);
}

// Very simple deletion, where the last agent is moved
// to the deleted agent, and the vec is shrunk by 1
void avec_delete(AVec *vec, size_t idx) {
  vec->agents[idx] = vec->agents[vec->size-1];
  vec->size--;
}

void avec_push_back(AVec *vec, Agent& a) {
  if (vec->size == vec->allocated) {
    vec->agents = (Agent*) realloc(vec->agents, sizeof(Agent) * vec->allocated*2);
    vec->allocated = vec->allocated * 2;
  }
  vec->agents[vec->size] = a;
  vec->size++;  
}

Agent& avec_get(AVec *vec, size_t idx) {
  return vec->agents[idx];
}
