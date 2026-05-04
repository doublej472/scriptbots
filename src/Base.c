#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Base.h"
#include "helpers.h"
#include "lock.h"

void base_init(struct Base *base, struct World *world) {
  base->world = world;
  base->world_file[0] = '\0';
}

void base_saveworld(struct Base *base) {
  queue_wait_until_done(base->world->queue);
  world_flush_staging(base->world);

  const char *fn = base->world_file[0] ? base->world_file : "world.dat";
  FILE *f = fopen(fn, "wb");
  printf("Saving world to %s...\n", fn);

  struct World *w = malloc(sizeof(struct World));
  memcpy(w, base->world, sizeof(struct World));
  w->agents.agents = NULL;
  w->agents_staging.agents = NULL;
  w->sorted_agents = NULL;
  w->sorted_capacity = 0;
  fwrite(w, sizeof(struct World), 1, f);
  free(w);

  printf("Writing %zu agents...\n", base->world->agents.size);
  fwrite(&base->world->agents.size, sizeof(long), 1, f);
  for (size_t i = 0; i < base->world->agents.size; i++)
    fwrite(base->world->agents.agents[i], sizeof(struct Agent), 1, f);

  fclose(f);
  printf("Done!\n");
}

int base_loadworld(struct Base *base) {
  queue_wait_until_done(base->world->queue);
  world_flush_staging(base->world);
  const char *fn = base->world_file[0] ? base->world_file : "world.dat";
  printf("Loading world from %s...\n", fn);
  FILE *f = fopen(fn, "rb");
  if (!f) {
    fprintf(stderr, "ERROR: cannot open '%s' for reading\n", fn);
    return 0;
  }

  world_free_agents(base->world);

  struct Queue *old_queue = base->world->queue;

  fread(base->world, sizeof(struct World), 1, f);
  base->world->stopSim = 0;

  long size = 0l;
  fread(&size, sizeof(long), 1, f);
  printf("Reading %ld agents...\n", size);

  avec_init(&base->world->agents, size);
  avec_init(&base->world->agents_staging, 16);

  base->world->agents.size = size;

  for (int i = 0; i < base->world->agents.size; i++) {
    base->world->agents.agents[i] = malloc(sizeof(struct Agent));
    fread(base->world->agents.agents[i], sizeof(struct Agent), 1, f);
  }

  printf("Fixing world struct...\n");

  base->world->queue = old_queue;
  base->world->brain_gpu = NULL;
  base->world->brain_slot = 0;
  base->world->sorted_agents = NULL;
  base->world->sorted_capacity = 0;
  base->world->sorted_size = 0;

  // Wait until we have no agents being worked on
  lock_lock(&base->world->queue->lock);

  base->world->queue->size = 0;
  base->world->queue->in = 0;
  base->world->queue->out = 0;
  base->world->queue->num_work_items = 0;
  lock_unlock(&base->world->queue->lock);

  world_flush_staging(base->world);
  world_sortGrid(base->world);

  fclose(f);
  printf("Done!\n");
  return 1;
}
