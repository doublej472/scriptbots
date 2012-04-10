#include "GLView.h"
#include "World.h"
#include <ctime>
#include "config.h"
#ifdef LOCAL_GLUT32
#include "glut.h"
#else
#ifdef MAC_GLUT
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#endif

#include <stdio.h>

// Include Boost serialization:
#include "boosty.h"

GLView* GLVIEW = new GLView(0);

int main(int argc, char **argv)
{
    srand(time(0));
    if (conf::WIDTH%conf::CZ!=0 || conf::HEIGHT%conf::CZ!=0)
	{
		printf("CAREFUL! The cell size variable conf::CZ should divide evenly into  both conf::WIDTH and ");
		printf("conf::HEIGHT! It doesn't right now!");    
    }
	
    printf("p= pause, d= toggle drawing (for faster computation), f= draw food too, += faster, -= slower\n");
    printf("Pan around by holding down right mouse button, and zoom by holding down middle button.\n");
	printf("Bot Status Colors: \nWHITE: they just ate part of another agent\n");
	printf("YELLOW: bot just spiked another bot\nGREEN: agent just reproduced\n");
	printf("GREY: bot is getting group health bonus\n");
	
    World* world = new World();
    GLVIEW->setWorld(world);

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

	// save data to archive -----------------------------
	std::cout << std::endl << std::endl << "Saving file now: " << std::endl;
	
	std::ofstream ofs("myworld"); // create and open a character archive for output
	
	boost::archive::text_oarchive oa(ofs);

	std::cout << "Number = " << (*world).numAgents() << std::endl;
	
	// write class instance to archive
	//	oa << (*world);
	oa << (*world);
	// archive and stream closed when destructors are called
	


    return 0;
}
