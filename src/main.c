#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// For detecting keyboard:
#include <fcntl.h>
#include <unistd.h>

#ifdef TRAP_NAN
#include <fenv.h>
#endif

// Determine if and what kind of graphics to use:
#ifdef OPENGL
#include "GLView.h"
#include <GL/glut.h>
#endif

#include "Base.h"
#include "World.h"
#include "helpers.h"
#include "queue.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Global Vars:

#ifdef OPENGL
struct GLView GLVIEW; // only use when graphic support is enabled
#endif

int32_t MAX_EPOCHS = INT_MAX; // inifinity
int32_t MAX_SECONDS = INT_MAX;
int32_t VERBOSE;
int32_t HEADLESS;
int32_t NUM_THREADS;

struct Base base;

// ---------------------------------------------------------------------------
// Prototypes:
void runHeadless(struct Base *base);
void runWithGraphics(int32_t argc, char **argv, struct Base *base);

void signal_handler(int signum) { base.world->stopSim = 1; }

void *worker_thread(void *arg) {
  struct Queue *queue = (struct Queue *)arg;

  init_thread_random();

  while (1) {
    struct QueueItem qi = queue_dequeue(queue);
    qi.function(qi.data);
    queue_workdone(queue);
  }
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
#ifdef TRAP_NAN
  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
  init_thread_random();
  VERBOSE = 0; // Run in verbose mode
#ifdef OPENGL
  HEADLESS = 0;
#else
  HEADLESS = 1;
#endif
  NUM_THREADS = get_nprocs();

  int32_t loadWorldFromFile = 0;

  // Retrieve command line arguments
  // -h: Run program headless
  // -v: Run program in verbose mode
  // -w: Load world state from file
  // -n: Specify number of threads
  // -e: Specify maximum epochs to run
  int32_t c;
  while ((c = getopt(argc, argv, "vhwn:e:s:")) != -1) {
    switch (c) {
    case 'h':
      HEADLESS = 1;
      break;
    case 'v':
      VERBOSE = 1;
      break;
    case 'w':
      loadWorldFromFile = 1;
      break;
    case 'n':
      NUM_THREADS = atoi(optarg);
      break;
    case 'e':
      MAX_EPOCHS = atoi(optarg);
      break;
    case 's':
      MAX_SECONDS = atoi(optarg);
      break;
    default:
      break;
    }
  }

  struct World world;
  world_init(&world);
  base_init(&base, &world);

  signal(SIGINT, signal_handler);

  printf("---------------------------------------------------------------------"
         "----------\n");
  printf("ScriptBots - Evolutionary Artificial Life Simulation of "
         "Predator-Prey Dynamics\n");
  printf("   Version 6 - by Andrej Karpathy, Dave Coleman, Gregory Hopkins, "
         "Jonathan Frederick\n\n");
  printf("Environment:\n");
#ifdef OPENGL
  printf("   OpenGL and GLUT supported!\n");
#else
  printf("   OpenGL and GLUT NOT supported!\n");
#endif

  printf("   Threading details:\n");
  printf("      %li processors available\n", get_nprocs());
  printf("      Using %i threads\n", NUM_THREADS);
  printf("   Termination:\n");
  if (MAX_EPOCHS < INT_MAX)
    printf("      Stopping at %i epochs\n", MAX_EPOCHS);
  else {
    if (HEADLESS) {
      printf("      Press ctrl+c to save and end simulation\n\n");
    } else {
      printf("      Press ESC to save and end simulation\n\n");
    }
  }

  if (WIDTH % CZ != 0 || HEIGHT % CZ != 0) {
    printf("   WARNING: The cell size variable CZ should divide evenly "
           "into WIDTH");
    printf(" and HEIGHT\n");
  }
  if (HEADLESS) {
    printf("   Headless Mode - No graphics\n");
    printf("------------------------------------------------------------------"
           "-------------\n");
  } else {
    printf("\nInstructions:\n");
    printf("   p= pause, d= toggle drawing (for faster computation), f= draw "
           "food too, += faster, -= slower\n");
    printf("   Pan around by holding down right mouse button, and zoom by "
           "holding down middle button.\n");
    printf("\nBot Status Colors: \n");
    printf("   WHITE: they just ate part of another agent\n");
    printf("   YELLOW: bot just spiked another bot\n");
    printf("   GREEN: agent just reproduced\n");
    printf("   GREY: bot is getting group health bonus\n");
    printf("------------------------------------------------------------------"
           "-------------\n");
  }

  pthread_t *threads = malloc(sizeof(pthread_t) * NUM_THREADS);

  for (int i = 0; i < NUM_THREADS; i++) {
    // printf("Creating thread\n");
    pthread_create(&threads[i], NULL, worker_thread, base.world->queue);
  }

  // Load file if needed
  if (loadWorldFromFile) {
    base_loadworld(&base);

    // check if epoch is greater than passed parameter
    if (base.world->current_epoch > MAX_EPOCHS)
      printf("\nWarning: the loaded file has an epoch later than the specefied "
             "end time parameter\n");
  }

  // Decide if to graphics or not
  if (HEADLESS) {
    runHeadless(&base);
  } else {
    runWithGraphics(argc, argv, &base);
  }

  queue_close(base.world->queue);

  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }

  free(base.world->queue);
  free(threads);

  return 0;
}

// ---------------------------------------------------------------------------
// Run Scriptbots with graphics
// ---------------------------------------------------------------------------
void runWithGraphics(int32_t argc, char **argv, struct Base *base) {

#ifdef OPENGL
  init_glview();
  GLVIEW.base = base;

  // GLUT SETUP
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
  glutInitWindowSize(WWIDTH, WHEIGHT);
  glutCreateWindow("Scriptbots");
  glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
  glutDisplayFunc(gl_renderScene);
  glutIdleFunc(gl_handleIdle);
  glutReshapeFunc(gl_changeSize);

  glutKeyboardFunc(gl_processNormalKeys);
  glutSpecialFunc(gl_processSpecialKeys);
  glutMouseFunc(gl_processMouse);
  glutMotionFunc(gl_processMouseActiveMotion);

  glutMainLoop(); // spin
#endif
}

// ---------------------------------------------------------------------------
// Run Scriptbots headless
// ---------------------------------------------------------------------------
void runHeadless(struct Base *base) {
  printf("Simulation Starting...\n");

  while (!base->world->stopSim) {
    world_update(base->world);
  }

  base_saveworld(base);
  for (int i = 0; i < base->world->agents.size; i++) {
    free_brain(base->world->agents.agents[i].brain);
  }
  for (int i = 0; i < base->world->agents_staging.size; i++) {
    free_brain(base->world->agents_staging.agents[i].brain);
  }
  avec_free(&base->world->agents);
  avec_free(&base->world->agents_staging);
}
