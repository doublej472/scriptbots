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
  char world_file[256];
};

void base_init(struct Base *base, struct World *world);
void base_saveworld(struct Base *base);
int  base_loadworld(struct Base *base);  // 1 on success, 0 on failure

#ifdef __cplusplus
}
#endif

#endif // BASE_H
