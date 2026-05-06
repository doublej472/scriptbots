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

#include "Base.h"
#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "settings.h"
#include "vkview.h"

int32_t VERBOSE;
int32_t NUM_THREADS;
struct Base base;

void signal_handler(int signum) { base.world->stopSim = 1; }

static void print_usage(void) {
  printf("Usage: scriptbots [OPTIONS]\n");
  printf("\n");
  printf("Options:\n");
  printf("  -h, --headless        Run headless (no window, Vulkan compute only)\n");
  printf("  -s, --steps N         Stop after N simulation steps (headless only)\n");
  printf("  -w, --world [FILE]    Load world from FILE (default: world.dat)\n");
  printf("  --help                Show this help message and exit\n");
  printf("\n");
  printf("Windowed controls:\n");
  printf("  p/pause  d/draw  f/food  +/- speed  arrows pan  pgup/pgdn zoom\n");
  printf("  right-drag pan  middle-click zoom  left-click select\n");
  printf("  Ctrl+S save  Ctrl+L load  r/reset  h/add herbivores  q/add carnivores\n");
  printf("  z/reset view  t/text  m/movie  c/toggle closed\n");
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
#ifdef TRAP_NAN
  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
  init_thread_random();
  VERBOSE = 0;
  NUM_THREADS = get_nprocs();
  if (NUM_THREADS > 1)
    NUM_THREADS--;
  if (NUM_THREADS < 1)
    NUM_THREADS = 1;

  int headless = 0;
  int load_world = 0;
  int max_steps = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--headless") == 0) {
      headless = 1;
    } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--steps") == 0) {
      if (i + 1 < argc) {
        max_steps = atoi(argv[++i]);
        if (max_steps < 0)
          max_steps = 0;
      }
    } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--world") == 0) {
      load_world = 1;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        strncpy(base.world_file, argv[++i], sizeof(base.world_file) - 1);
      }
    }
  }

  struct World *world = malloc(sizeof(struct World));
  world_alloc(world); // minimal setup: queue, zeroing
  base_init(&base, world);

  if (load_world) {
    if (!base_loadworld(&base)) {
      fprintf(stderr, "Falling back to new world.\n");
      world_populate(world, /*initFood=*/1, NUMBOTS);
      load_world = 0;
    }
  } else {
    world_populate(world, /*initFood=*/1, NUMBOTS);
  }

  signal(SIGINT, signal_handler);

  if (headless) {
    printf("---------------------------------------------------------------------\n");
    printf("ScriptBots - Evolutionary Artificial Life Simulation\n");
    printf("   Version 7 — Vulkan + GPU Brain [HEADLESS]\n");
    printf("   %li processors available, %i threads\n", get_nprocs(), NUM_THREADS);
    printf("   Press Ctrl+C to save and end simulation\n");
    printf("---------------------------------------------------------------------\n");
  } else {
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
  }

  pthread_t *threads = malloc(sizeof(pthread_t) * NUM_THREADS);
  // Workers live in queue.c; they wake on cond broadcast, claim batches via atomic cursor
  for (int i = 0; i < NUM_THREADS; i++)
    pthread_create(&threads[i], NULL, worker_thread, base.world->queue);
  base.world->queue->num_participants = NUM_THREADS + 1;

  if (headless) {
    // ---- Headless path: Vulkan compute only, no window / swapchain / rendering ----
    memset(&VKVIEW, 0, sizeof(VKVIEW));
    VKVIEW.max_steps = max_steps;
    VKVIEW.vkstate = vkinit_create_headless();
    if (!VKVIEW.vkstate) {
      fprintf(stderr, "FATAL: Headless Vulkan init failed\n");
      return 1;
    }
    VKVIEW.base = &base;
    base.world->brain_gpu = VKVIEW.vkstate;

    // Assign GPU slots and upload brains (same as windowed path)
    if (load_world) {
      for (size_t i = 0; i < base.world->agents.size; i++) {
        struct Agent *a = base.world->agents.agents[i];
        a->brain_chunk = ~0u;
        if (!vkbrain_assign_slot(VKVIEW.vkstate, a, &a->brain_chunk, &a->brain_index)) {
          fprintf(stderr, "GPU memory exhausted at agent %zu\n", i);
          break;
        }
      }
    }
    vkbrain_upload_all(VKVIEW.vkstate, base.world);
    world_seed_inputs(base.world);
    vkbrain_record_dispatch(VKVIEW.vkstate, 0);

    vkview_main_loop_headless();

    // Cleanup headless Vulkan
    vkDeviceWaitIdle(vkinit_get_device(VKVIEW.vkstate));
    vkinit_destroy_headless(VKVIEW.vkstate);
  } else {
    // ---- Windowed path: full GLFW + Vulkan graphics + ImGui ----
    vkview_init(argc, argv);
    VKVIEW.base = &base;
    base.world->brain_gpu = VKVIEW.vkstate;

    if (VKVIEW.vkstate) {
      if (load_world) {
        for (size_t i = 0; i < base.world->agents.size; i++) {
          struct Agent *a = base.world->agents.agents[i];
          a->brain_chunk = ~0u;
          if (!vkbrain_assign_slot(VKVIEW.vkstate, a, &a->brain_chunk, &a->brain_index)) {
            fprintf(stderr, "GPU memory exhausted at agent %zu\n", i);
            break;
          }
        }
      }
      vkbrain_upload_all(VKVIEW.vkstate, base.world);
      world_seed_inputs(base.world);
      vkbrain_record_dispatch(VKVIEW.vkstate, 0);
    }

    vkview_main_loop();
    vkview_cleanup();
  }

  // brain_gpu was destroyed by vkview_cleanup / vkinit_destroy_headless — null
  // it so world_flush_staging (called by base_saveworld) skips GPU path
  base.world->brain_gpu = NULL;
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
