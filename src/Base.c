#include <stdio.h>
#include "include/Base.h"

void base_init(struct Base *base, struct World *world) {
  base->world = world;
}

void base_saveworld(struct Base *base) {
  printf("Fake world save!\n");
}

void base_loadworld(struct Base *base) {
  printf("Fake world load!\n");
}
