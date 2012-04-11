#include <ctime>
#include <stdio.h>

#include "config.h"
#include "World.h"
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
GLView* GLVIEW = new GLView();
#endif

// ---------------------------------------------------------------------------
// Prototypes:
int kbhit();
static inline void loadBar(int x, int n, int r, int w);

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
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
#endif
	if (conf::WIDTH%conf::CZ!=0 || conf::HEIGHT%conf::CZ!=0)
		printf("   WARNING: The cell size variable conf::CZ should divide evenly into conf::WIDTH and conf::HEIGHT\n");
	
#if OPENGL
	printf("\nInstructions:\n");
	printf("   p= pause, d= toggle drawing (for faster computation), f= draw food too, += faster, -= slower\n");
	printf("   Pan around by holding down right mouse button, and zoom by holding down middle button.\n");
	printf("\nBot Status Colors: \n");
	printf("   WHITE: they just ate part of another agent\n");
	printf("   YELLOW: bot just spiked another bot\n");
	printf("   GREEN: agent just reproduced\n");
	printf("   GREY: bot is getting group health bonus\n");
#else
	cout << "   Headless Mode - No graphics\n";
	cout << "      Press any key to save and end simulation\n" << endl;	
#endif
	cout << "-------------------------------------------------------------------------------" << endl;	
	
	srand(time(0));

	Base base;

	// If any argument is passed, just load the file
	if( argc > 1 )
	{
		base.loadWorld();
	}	

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

	glutMainLoop();
#else
	while( !kbhit() )
	{
		base.world->update();

		//loadBar(base.world->,  60, 1, 60);
		
	}
   	//base.runWorld(12);

	base.saveWorld();

#endif

					
	return 0;
}

// ---------------------------------------------------------------------------
// Process has done i out of n rounds,
// and we want a bar of width w and resolution r.
// ---------------------------------------------------------------------------
static inline void loadBar(int x, int n, int r, int w)
{
	// Only update r times.
	if ( x % (n/r) != 0 ) return;

	// Calculuate the ratio of complete-to-incomplete.
	float ratio = x/(float)n;
	int   c     = ratio * w;

	// Show the percentage complete.
	printf("%3d%% [", (int)(ratio*100) );

	// Show the load bar.
	for (int x=0; x<c; x++)
		printf("=");

	for (int x=c; x<w; x++)
		printf(" ");

	// ANSI Control codes to go back to the
	// previous line and clear it.
	printf("]\n\033[F\033[J");
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
