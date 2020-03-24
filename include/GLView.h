#ifndef GLVIEW_H
#define GLVIEW_H
#include "Base.h"

struct GLView {
  struct Base *base;
  int paused;
  int draw;
  int skipdraw;
  int drawfood;
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
};

extern struct GLView GLVIEW;

void init_glview();
void gl_processMouse(int button, int state, int x, int y);
void gl_processMouseActiveMotion(int x, int y);
void gl_changeSize(int w, int h);
void gl_processNormalKeys(unsigned char key, int x, int y);
void gl_handleIdle();
void gl_renderScene();
void drawAgent(const struct Agent *agent);
void drawFood(int x, int y, float quantity);
void glview_draw(struct World *world, int drawfood);

#endif // GLVIEW_H
