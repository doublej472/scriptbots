#include <ctime>
#include <getopt.h>
#include <stdio.h>

#include "config.h"

#include "include/PerfTimer.h"
#include "include/World.h"
#include "include/settings.h"
#include <omp.h>

// Include Boost serialization:
#include "include/boost.h"

// Determine if and what kind of graphics to use:
#ifdef OPENGL
#include "include/GLView.h"
#ifdef LOCAL_GLUT32
#include "include/glut.h"
#else
#ifdef MAC_GLUT
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#endif
#else
#include "include/Base.h" // this is normally included in GLView.h
#endif

// For detecting keyboard:
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

using namespace std;

// ---------------------------------------------------------------------------
// Global Vars:

#ifdef OPENGL
GLView *GLVIEW = new GLView(); // only use when graphic support is enabled
#endif

int MAX_EPOCHS = INT_MAX; // inifinity
int MAX_SECONDS = INT_MAX;
bool VERBOSE;
bool HEADLESS;
int NUM_THREADS;
PerfTimer TIMER; // used throughout program to do benchmark timing

// ---------------------------------------------------------------------------
// Prototypes:
int kbhit();
void runHeadless(Base &base);
void runWithGraphics(int &argc, char **argv, Base &base);

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  TIMER = PerfTimer();
  VERBOSE = false; // Run in verbose mode
#ifdef OPENGL
  HEADLESS = false;
#else
  HEADLESS = true;
#endif
#ifdef OPENMP
  NUM_THREADS =
      omp_get_num_procs(); // Specifies the number of threads to use
                           // Defaults to the number of available processors
#endif
  bool loadWorldFromFile = false;

  // Retrieve command line arguments
  // -h: Run program headless
  // -v: Run program in verbose mode
  // -w: Load world state from file
  // -n: Specify number of threads
  // -e: Specify maximum epochs to run
  int c;
  while ((c = getopt(argc, argv, "vhwn:e:s:")) != -1) {
    switch (c) {
    case 'h':
      HEADLESS = true;
      break;
    case 'v':
      VERBOSE = true;
      break;
    case 'w':
      loadWorldFromFile = true;
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

  Base base;

  cout << "--------------------------------------------------------------------"
          "-----------"
       << endl;
  cout << "ScriptBots - Evolutionary Artificial Life Simulation of "
          "Predator-Prey Dynamics"
       << endl;
  cout << "   Version 5 - by Andrej Karpathy, Dave Coleman, Gregory Hopkins"
       << endl
       << endl;
  cout << "Environment:" << endl;
#ifdef OPENGL
  cout << "   OpenGL and GLUT found!" << endl;
#else
  cout << "   OpenGL and GLUT NOT found!" << endl;
#endif
#ifdef OPENMP
  cout << "   OpenMP found!" << endl;
  cout << "      " << omp_get_num_procs() << " processors available" << endl;
  cout << "      Using " << NUM_THREADS << " threads" << endl;
  cout << "   Termination:" << endl;
  if (MAX_EPOCHS < INT_MAX)
    cout << "      Stopping at " << MAX_EPOCHS << " epochs" << endl;
  else
    cout << "      Press any key to save and end simulation\n" << endl;
#else
  cout << "   OpenMP NOT found!" << endl;
#endif
  if (conf::WIDTH % conf::CZ != 0 || conf::HEIGHT % conf::CZ != 0) {
    printf("   WARNING: The cell size variable conf::CZ should divide evenly "
           "into conf::WIDTH");
    printf(" and conf::HEIGHT\n");
  }
  if (HEADLESS) {
    cout << "   Headless Mode - No graphics\n";
    cout << "------------------------------------------------------------------"
            "-------------"
         << endl;
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
    cout << "------------------------------------------------------------------"
            "-------------"
         << endl;
  }

  srand(time(0));

  // Load file if needed
  if (loadWorldFromFile) {
    base.loadWorld();

    // check if epoch is greater than passed parameter
    if (base.world->epoch() > MAX_EPOCHS)
      cout << endl
           << "Warning: the loaded file has an epoch later than the specefied "
              "end time parameter"
           << endl;
  }

  // Decide if to graphics or not
  if (HEADLESS) {
    runHeadless(base);
  } else {
    runWithGraphics(argc, argv, base);
  }

  return 0;
}

// ---------------------------------------------------------------------------
// Used for detecting keyboard end
// Cross-platform?
// ---------------------------------------------------------------------------
int kbhit() {
  struct termios oldt, newt;
  int ch;
  int oldf;

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
void runWithGraphics(int &argc, char **argv, Base &base) {

#ifdef OPENGL
  GLVIEW->setBase(&base);

  // GLUT SETUP
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
  glutInitWindowSize(conf::WWIDTH, conf::WHEIGHT);
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
void runHeadless(Base &base) {

  if (VERBOSE)
    TIMER.start("total");

  printf("Simulation Starting...\r");
  while (!kbhit() && !base.world->stopSim) {
    base.world->update();
  }

  if (VERBOSE) {
    TIMER.end("total");
    TIMER.printTimes();
  }

  base.saveWorld();
}
