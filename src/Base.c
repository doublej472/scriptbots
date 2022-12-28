#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Base.h"
#include "lock.h"

void base_init(struct Base *base, struct World *world) { base->world = world; }

void base_saveworld(struct Base *base) {
  // Wait until we have no agents being worked on
  queue_wait_until_done(&base->world->queue);
  world_flush_staging(base->world);

  FILE *f = fopen("world.dat", "wb");
  printf("Saving world to world.dat...\n");

  struct World w;
  memcpy(&w, base->world, sizeof(struct World));
  // Clean up pointers
  w.agents.agents = NULL;
  w.agents_staging.agents = NULL;
  fwrite(&w, sizeof(struct World), 1, f);

  printf("Writing %zu agents...\n", base->world->agents.size);
  fwrite(&base->world->agents.size, sizeof(long), 1, f);
  fwrite(base->world->agents.agents, sizeof(struct Agent),
         base->world->agents.size, f);

  printf("Writing %zu brains...\n", base->world->agents.size);
  for (int i = 0; i < base->world->agents.size; i++) {
    fwrite(base->world->agents.agents[i].brain, sizeof(struct AVXBrain), 1, f);
  }

  fclose(f);
  printf("Done!\n");
}

void base_loadworld(struct Base *base) {
  // Wait until we have no agents being worked on
  queue_wait_until_done(&base->world->queue);
  world_flush_staging(base->world);
  printf("Loading world from world.dat...\n");
  FILE *f = fopen("world.dat", "rb");

  for (int i = 0; i < base->world->agents.size; i++) {
    free_brain(base->world->agents.agents[i].brain);
  }

  avec_free(&base->world->agents);
  avec_free(&base->world->agents_staging);

  struct Queue old_queue;
  memcpy(&old_queue, &base->world->queue, sizeof(struct Queue));

  fread(base->world, sizeof(struct World), 1, f);
  base->world->stopSim = 0;

  long size = 0l;
  fread(&size, sizeof(long), 1, f);
  printf("Reading %ld agents...\n", size);

  avec_init(&base->world->agents, size);
  avec_init(&base->world->agents_staging, 16);

  fread(base->world->agents.agents, sizeof(struct Agent), size, f);
  base->world->agents.size = size;

  printf("Reading %ld brains...\n", size);
  for (int i = 0; i < base->world->agents.size; i++) {
    base->world->agents.agents[i].brain =
        alloc_aligned(32, sizeof(struct AVXBrain));
    fread(base->world->agents.agents[i].brain, sizeof(struct AVXBrain), 1, f);
  }

  printf("Fixing world struct...\n");
  clock_gettime(CLOCK_MONOTONIC, &base->world->startTime);
  clock_gettime(CLOCK_MONOTONIC, &base->world->totalStartTime);

  memcpy(&base->world->queue, &old_queue, sizeof(struct Queue));

  // Wait until we have no agents being worked on
  lock_lock(&base->world->queue.lock);

  base->world->queue.size = 0;
  base->world->queue.in = 0;
  base->world->queue.out = 0;
  base->world->queue.num_work_items = 0;
  lock_unlock(&base->world->queue.lock);

  world_flush_staging(base->world);
  world_sortGrid(base->world);

  fclose(f);
  printf("Done!\n");
}
