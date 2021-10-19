#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "include/Base.h"

void base_init(struct Base *base, struct World *world) {
  base->world = world;
}

void base_saveworld(struct Base *base) {
  world_flush_staging(base->world);

  FILE *f = fopen("world.dat", "wb");
  printf("Saving world to world.dat...\n");

  struct World w;
  memcpy(&w, base->world, sizeof(struct World));
  // Clean up pointers
  w.agents.agents = NULL;
  w.agents_staging.agents = NULL;
  fwrite(&w, sizeof(struct World), 1, f);

  printf("Writing %ld agents...\n", base->world->agents.size);
  fwrite(&base->world->agents.size, sizeof(long), 1, f);
  fwrite(base->world->agents.agents, sizeof(struct Agent), base->world->agents.size, f);

  fclose(f);
  printf("Done!\n");
}

void base_loadworld(struct Base *base) {
  printf("Loading world from world.dat...\n");
  FILE *f = fopen("world.dat", "rb");

  avec_free(&base->world->agents);
  avec_free(&base->world->agents_staging);

  fread(base->world, sizeof(struct World), 1, f);

  long size = 0l;
  fread(&size, sizeof(long), 1, f);
  printf("Reading %ld agents from agents...\n", size);
  avec_init(&base->world->agents, size);
  avec_init(&base->world->agents_staging, 16);
  fread(base->world->agents.agents, sizeof(struct Agent), size, f);
  base->world->agents.size = size;

  printf("Fixing world struct...\n");
  clock_gettime(CLOCK_MONOTONIC_RAW, &base->world->startTime);
  clock_gettime(CLOCK_MONOTONIC_RAW, &base->world->totalStartTime);
  for (size_t i = 0; i < base->world->agents.size; i++) {
    base->world->agents.agents[i].num_close_agents = 0;
    base->world->agents.agents[i].close_agents = malloc(sizeof(struct Agent_d) * NUMBOTS_CLOSE);
  }

  fclose(f);
  printf("Done!\n");
}
