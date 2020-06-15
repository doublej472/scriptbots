#include <time.h>
#include <getopt.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// For detecting keyboard:
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"

#ifdef OPENMP
#include <omp.h>
#endif

// Determine if and what kind of graphics to use:
#ifdef OPENGL
#include <GL/glut.h>
#include "include/GLView.h"
#endif


#include "include/Base.h"
#include "include/World.h"
#include "include/helpers.h"
#include "include/settings.h"


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

// ---------------------------------------------------------------------------
// Prototypes:
int32_t kbhit();
void runHeadless(struct Base *base);
void runWithGraphics(int32_t argc, char **argv, struct Base *base);

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  VERBOSE = 0; // Run in verbose mode
#ifdef OPENGL
  HEADLESS = 0;
#else
  HEADLESS = 1;
#endif
#ifdef OPENMP
  NUM_THREADS = get_nprocs();
#endif
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

  #ifdef OPENMP
  // Set the number of threads now, just once, here:
  omp_set_num_threads(NUM_THREADS);
  #endif

  struct Base base;
  struct World world;
  world_init(&world);
  base_init(&base, &world);

  printf("-------------------------------------------------------------------------------\n");
  printf("ScriptBots - Evolutionary Artificial Life Simulation of Predator-Prey Dynamics\n");
  printf("   Version 5 - by Andrej Karpathy, Dave Coleman, Gregory Hopkins\n\n");
  printf("Environment:");
#ifdef OPENGL
  printf("   OpenGL and GLUT found!\n");
#else
  printf("   OpenGL and GLUT NOT found!\n");
#endif
#ifdef OPENMP
  printf("   OpenMP found!\n");
  printf("      %li processors available\n", get_nprocs());
  printf("      Using %i threads\n", NUM_THREADS);
  printf("   Termination:\n");
  if (MAX_EPOCHS < INT_MAX)
    printf("      Stopping at %i epochs\n", MAX_EPOCHS);
  else
    printf("      Press any key to save and end simulation\n\n");
#else
  printf("   OpenMP NOT found!\n");
#endif
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

  srand(time(0));

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

  return 0;
}

// ---------------------------------------------------------------------------
// Used for detecting keyboard end
// Cross-platform?
// ---------------------------------------------------------------------------
int32_t kbhit() {
  struct termios oldt, newt;
  int32_t ch;
  int32_t oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if (ch != EOF) {
    ungetc(ch, stdin);
    return 1;
  }

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
  glutMouseFunc(gl_processMouse);
  glutMotionFunc(gl_processMouseActiveMotion);

  glutMainLoop(); // spin
#endif
}

// ---------------------------------------------------------------------------
// Run Scriptbots headless
// ---------------------------------------------------------------------------
void runHeadless(struct Base *base) {

  printf("Simulation Starting...\r");
  while (!kbhit() && !base->world->stopSim) {
    world_update(base->world);
  }

  base_saveworld(base);
}
