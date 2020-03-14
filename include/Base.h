#ifndef BASE_H
#define BASE_H
#include "World.h"

class Base {
public:
  World *world;

  Base();
  void saveWorld();
  void loadWorld();
};

#endif // BASE_H
