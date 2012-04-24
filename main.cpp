#include <ctime>
#include <stdio.h>
#include <getopt.h>

#include "config.h"
#include "World.h"
#include "settings.h"
#include "PerfTimer.h"
#include "omp.h"

// Include Boost serialization:
#include "boost.h"

// Determine if and what kind of graphics to use:
#if OPENGL
#include "GLView.h"
#ifdef LOCAL_GLUT32
#include "glut.h"
#else
#ifdef MAC_GLUT
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#endif
#else
#include "Base.h" // this is normally included in GLView.h
#endif

// For detecting keyboard:
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

// ---------------------------------------------------------------------------
// Global Vars:

#if OPENGL
GLView* GLVIEW = new GLView(); // only use when graphic support is enabled
#endif

int MAX_EPOCHS = INT_MAX; // inifinity
bool VERBOSE;
bool HEADLESS;
int NUM_THREADS;
PerfTimer TIMER; // used throughout program to do benchmark timing

// ---------------------------------------------------------------------------
// Prototypes:
int kbhit();
void runHeadless(Base &base);
void runWithGraphics(int &argc, char** argv, Base &base);

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
	TIMER = PerfTimer();
	VERBOSE = false; // Run in verbose mode
	HEADLESS = false; // Run without graphics even if OpenGL and GLUT available
	NUM_THREADS = omp_get_num_procs(); // Specifies the number of threads to use
									   // Defaults to the number of available processors
	Base base;
	
	bool loadWorldFromFile = false;
	
	// Retrieve command line arguments
	// -h: Run program headless
	// -v: Run program in verbose mode
	// -w: Load world state from file
	// -n: Specify number of threads
	// -e: Specify maximum epochs to run
	int c;
	while( (c = getopt(argc, argv, "vhwn:e:")) != -1){
		switch(c){
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
		default:
			break;
		}
	}

	// Set the number of threads now, just once, here:
	omp_set_num_threads(NUM_THREADS);	

	cout << "-------------------------------------------------------------------------------" << endl;
	cout << "ScriptBots - Evolutionary Artificial Life Simulation of Predator-Prey Dynamics" << endl;
	cout << "   Version 5 - by Andrej Karpathy, Dave Coleman, Gregory Hopkins" << endl << endl;
	cout << "Environment:" << endl;
	#if OPENGL
	cout << "   OpenGL and GLUT found." << endl;
	#endif
	#if OPENMP
	cout << "   OpenMP found." << endl;
	cout << "      " << omp_get_num_procs()	<< " processors available" << endl;
	cout << "   Using " << NUM_THREADS << " threads" << endl;
	#endif
	if (conf::WIDTH%conf::CZ!=0 || conf::HEIGHT%conf::CZ!=0)
	{
		printf("   WARNING: The cell size variable conf::CZ should divide evenly into conf::WIDTH");
		printf(" and conf::HEIGHT\n");
	}	
	
	srand(time(0));

	// Load file if needed
	if(loadWorldFromFile)
		base.loadWorld();

	// Decide if to graphics or not
	#if OPENGL
	if(HEADLESS)
	{
		runHeadless(base);
	}
	else
	{
		runWithGraphics(argc, argv, base);
	}
	#else
	runHeadless(base);
	#endif

					
	return 0;
}

// ---------------------------------------------------------------------------
// Used for detecting keyboard end
// Cross-platform?
// --------------------------------------------------------------------------- 
int kbhit()
{
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

	if(ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}

	return 0;
}

// ---------------------------------------------------------------------------
// Run Scriptbots with graphics
// --------------------------------------------------------------------------- 
void runWithGraphics(int &argc, char** argv, Base &base){
	printf("\nInstructions:\n");
	printf("   p= pause, d= toggle drawing (for faster computation), f= draw food too, += faster, -= slower\n");
	printf("   Pan around by holding down right mouse button, and zoom by holding down middle button.\n");
	printf("\nBot Status Colors: \n");
	printf("   WHITE: they just ate part of another agent\n");
	printf("   YELLOW: bot just spiked another bot\n");
	printf("   GREEN: agent just reproduced\n");
	printf("   GREY: bot is getting group health bonus\n");
	cout << "-------------------------------------------------------------------------------" << endl;	

	#if OPENGL	
	GLVIEW->setBase(&base);

	//GLUT SETUP
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(30,30);
	glutInitWindowSize(conf::WWIDTH,conf::WHEIGHT);
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
void runHeadless(Base &base){
	cout << "   Headless Mode - No graphics\n";
	cout << "      Press any key to save and end simulation\n" << endl;	
	cout << "-------------------------------------------------------------------------------" << endl;	
	
	if(VERBOSE)
		TIMER.start("total");

  	printf("Simulation Loading...\r");	
	while( !kbhit() && base.world->epoch() < MAX_EPOCHS)
	{
		base.world->update();		
	}
   	
   	if(VERBOSE)
   	{
		TIMER.end("total");
		TIMER.printTimes();
	}

	base.saveWorld();
}
