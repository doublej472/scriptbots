#ifndef BASE_H
#define BASE_H
#include "World.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SerializedWorld {
  struct World world;
};

struct Base {
  struct World *world;
};

void base_init(struct Base *base, struct World *world);
void base_saveworld(struct Base *base);
void base_loadworld(struct Base *base);

#ifdef __cplusplus
}
#endif

#endif // BASE_H
