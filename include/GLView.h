#ifndef GLVIEW_H
#define GLVIEW_H

#include "Base.h"
#include "View.h"
#include "World.h"

class GLView;

extern GLView *GLVIEW;

void gl_processNormalKeys(unsigned char key, int x, int y);
void gl_processMouse(int button, int state, int x, int y);
void gl_processMouseActiveMotion(int x, int y);
void gl_changeSize(int w, int h);
void gl_handleIdle();
void gl_renderScene();

class GLView : public View {

public:
  GLView(); // World* w);
  ~GLView();

  virtual void drawAgent(const Agent &a);
  virtual void drawFood(int x, int y, float quantity);

  void setBase(Base *b);

  // GLUT functions
  void processNormalKeys(unsigned char key, int x, int y);
  void processMouse(int button, int state, int x, int y);
  void processMouseActiveMotion(int x, int y);
  void changeSize(int w, int h);
  void handleIdle();
  void renderScene();
  void toggleFullscreen();

private:
  //    World *world;
  Base *base;
  bool paused;
  bool draw;
  int skipdraw;
  bool drawfood;
  bool drawconns;
  char buf[100];
  char buf2[10];
  int modcounter;
  int lastUpdate;
  int frames;

  float scalemult;
  float xtranslate, ytranslate;
  int downb[3];
  int mousex, mousey;
  int wwidth, wheight;
  bool is_fullscreen;
  int prev_width, prev_height;
};

#endif // GLVIEW_H
