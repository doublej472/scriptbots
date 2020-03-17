#ifndef BASE_H
#define BASE_H
#include "World.h"

struct Base {
  World *world;
};

void base_init(Base& base);
void base_saveworld(Base& base);
void base_loadworld(Base& base);

#endif // BASE_H
