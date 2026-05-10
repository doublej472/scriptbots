#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Base.h"
#include "helpers.h"

void base_init(struct Base *base, struct World *world) {
  base->world = world;
  base->world_file[0] = '\0';
}

void base_saveworld(struct Base *base) {
  // Workers idle (main thread between dispatches, or loop exited)
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
  w->agent_render_data = NULL;
  w->agent_render_capacity = 0;
  w->agent_inputs = NULL;
  w->selected_agent = NULL;
  w->movie_agent = NULL;
  w->foodGrid.food_amounts = NULL;
  w->foodGrid.food_indices = NULL;
  w->foodGrid.food_sorted  = NULL;
  fwrite(w, sizeof(struct World), 1, f);
  free(w);

  // Save food grid heap arrays (pointers were nulled above so they aren't
  // written as stale addresses inside the World blob).
  uint32_t food_cells = base->world->foodGrid.total_cells;
  fwrite(&food_cells, sizeof(uint32_t), 1, f);
  fwrite(base->world->foodGrid.food_amounts, sizeof(float), food_cells, f);
  fwrite(base->world->foodGrid.food_indices, sizeof(uint32_t), food_cells, f);
  fwrite(base->world->foodGrid.food_sorted, sizeof(uint32_t), food_cells, f);

  printf("Writing %zu agents...\n", base->world->agents.size);
  fwrite(&base->world->agents.size, sizeof(long), 1, f);
  for (size_t i = 0; i < base->world->agents.size; i++)
    fwrite(base->world->agents.agents[i], sizeof(struct Agent), 1, f);

  fclose(f);
  printf("Done!\n");
}

int base_loadworld(struct Base *base) {
  // Workers should be idle (no dispatch in progress after main loop stops)
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

  // Restore food grid heap arrays (written after the World blob by save).
  uint32_t food_cells = 0;
  fread(&food_cells, sizeof(uint32_t), 1, f);
  base->world->foodGrid.food_amounts = malloc(food_cells * sizeof(float));
  base->world->foodGrid.food_indices = malloc(food_cells * sizeof(uint32_t));
  base->world->foodGrid.food_sorted  = malloc(food_cells * sizeof(uint32_t));
  fread(base->world->foodGrid.food_amounts, sizeof(float), food_cells, f);
  fread(base->world->foodGrid.food_indices, sizeof(uint32_t), food_cells, f);
  fread(base->world->foodGrid.food_sorted, sizeof(uint32_t), food_cells, f);

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

  // Reset fields not serialised (or stale from older save formats)
  for (int i = 0; i < base->world->agents.size; i++) {
    base->world->agents.agents[i]->pending_health_delta = 0.0f;
    base->world->agents.agents[i]->food_request = 0.0f;
    base->world->agents.agents[i]->eating = 0.0f;
  }

  base->world->queue = old_queue;
  base->world->brain_gpu = NULL;
  base->world->brain_slot = 0;
  base->world->sorted_agents = NULL;
  base->world->sorted_capacity = 0;
  base->world->sorted_size = 0;
  base->world->agent_render_data = NULL;
  base->world->agent_render_capacity = 0;
  base->world->agent_inputs = NULL;
  base->world->selected_index = 0;
  base->world->movie_index = 0;
  base->world->selected_agent = NULL;
  base->world->movie_agent = NULL;

  // Reset queue state (old code reset lock-protected counters)
  base->world->queue->cursor = 0;
  base->world->queue->done_count = 0;

  world_flush_staging(base->world);
  world_render_populate_all(base->world);
  world_sortGrid(base->world);

  fclose(f);
  printf("Done!\n");
  return 1;
}
