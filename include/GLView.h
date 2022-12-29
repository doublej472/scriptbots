#ifndef GLVIEW_H
#define GLVIEW_H
#include "Base.h"
#include <stdint.h>

struct GLView {
  struct Base *base;
  int32_t paused;
  int32_t draw;
  int32_t skipdraw;
  int32_t drawfood;
  char buf[100];
  char buf2[256];
  int32_t modcounter;
  int32_t lastUpdate;
  int32_t frames;

  float scalemult;
  float xtranslate, ytranslate;
  int32_t downb[3];
  int32_t mousex, mousey;
  int32_t wwidth, wheight;
  int32_t is_fullscreen;
  int32_t prev_width, prev_height;
  int32_t draw_text;
};

extern struct GLView GLVIEW;

void init_glview(int32_t argc, char **argv);
void gl_processMouse(int32_t button, int32_t state, int32_t x, int32_t y);
void gl_processMouseActiveMotion(int32_t x, int32_t y);
void gl_changeSize(int32_t w, int32_t h);
void gl_processNormalKeys(unsigned char key, int32_t x, int32_t y);
void gl_processSpecialKeys(int key, int x, int y);
void gl_handleIdle();
void gl_renderScene();
void drawAgent(const struct Agent *agent);
void drawFood(int32_t x, int32_t y, float quantity);
void glview_draw(struct World *world, int32_t drawfood);
void glview_toggleFullscreen();

#endif // GLVIEW_H
