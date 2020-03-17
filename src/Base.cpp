#include "include/Base.h"

void base_init(Base& base) {
  base.world = new World();
}

void base_saveworld(Base& base) {
  printf("Fake world save!\n");
}

void base_loadworld(Base& base) {
  printf("Fake world load!\n");
}
