#include "World.h"
//#include "Base.h"

#include <ctime>
#include "config.h"

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
#endif

#include <stdio.h>

// Include Boost serialization:
#include "boost.h"

using namespace std;

#if OPENGL
GLView* GLVIEW = new GLView();
#endif

int main(int argc, char **argv) {
	cout << "-------------------------------------------------------------------------------" << endl;
	cout << "ScriptBots - Evolutionary Artificial Life Simulation of Predator-Prey Dynamics" << endl;
	cout << "   Version 5 - by Andrej Karpathy, Dave Coleman, Gregory Hopkins" << endl << endl;
	cout << "Environment:" << endl;
#if OPENGL
	cout << "   OpenGL and GLUT found." << endl;
#endif
#if OPENMP
	cout << "   OpenMP found." << endl;
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
	//		world->update();
	base.runWorld(12);

	base.saveWorld();

#endif

					
	return 0;
}
