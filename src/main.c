#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef TRAP_NAN
#include <fenv.h>
#endif

#include "vkview.h"
#include "Base.h"
#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "settings.h"

int32_t VERBOSE;
int32_t NUM_THREADS;
struct Base base;

void signal_handler(int signum) { base.world->stopSim = 1; }

void *worker_thread(void *arg) {
  struct Queue *queue = (struct Queue *)arg;
  init_thread_random();
  while (1) {
    struct QueueItem qi = queue_dequeue(queue);
    assert(qi.data != NULL);
    assert(qi.function != NULL);
    qi.function(qi.data);
    queue_workdone(queue);
  }
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  (void)argc; (void)argv;
#ifdef TRAP_NAN
  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
  init_thread_random();
  VERBOSE = 0;
  NUM_THREADS = get_nprocs();
  if (NUM_THREADS > 1) NUM_THREADS--;  // leave one core for main thread
  if (NUM_THREADS < 1)  NUM_THREADS = 1;

  struct World *world = malloc(sizeof(struct World));
  world_init(world, /*initFood=*/1, NUMBOTS);
  base_init(&base, world);

  signal(SIGINT, signal_handler);

  printf("---------------------------------------------------------------------\n");
  printf("ScriptBots - Evolutionary Artificial Life Simulation\n");
  printf("   Version 7 — Vulkan + GPU Brain\n");
  printf("   %li processors available, %i threads\n", get_nprocs(), NUM_THREADS);
  printf("   Press ESC to save and end simulation\n");
  printf("---------------------------------------------------------------------\n");
  printf("\nControls:\n");
  printf("   p/pause  d/draw  f/food  +/- speed  arrows pan  pgup/pgdn zoom\n");
  printf("   right-drag pan  middle-click zoom  left-click select\n");
  printf("   Ctrl+S save  Ctrl+L load  r/reset  h/add herbivores  q/add carnivores\n");
  printf("   z/reset view  t/text  m/movie  c/toggle closed\n");
  printf("---------------------------------------------------------------------\n");

  pthread_t *threads = malloc(sizeof(pthread_t) * NUM_THREADS);
  for (int i = 0; i < NUM_THREADS; i++)
    pthread_create(&threads[i], NULL, worker_thread, base.world->queue);

  vkview_init(argc, argv);
  VKVIEW.base = &base;
  base.world->brain_gpu = VKVIEW.vkstate;

  // Upload initial brains to GPU, then record first dispatch for slot 0
  if (VKVIEW.vkstate) {
    vkbrain_upload_all(VKVIEW.vkstate, base.world);
    vkbrain_record_dispatch(VKVIEW.vkstate, (int)base.world->agents.size, 0);
  }

  vkview_main_loop();
  vkview_cleanup();

  base_saveworld(&base);
  queue_close(base.world->queue);

  world_free_agents(base.world);

  for (int i = 0; i < NUM_THREADS; i++)
    pthread_join(threads[i], NULL);

  free(base.world->queue);
  free(base.world->sorted_agents);
  free(base.world);
  free(threads);
  return 0;
}
