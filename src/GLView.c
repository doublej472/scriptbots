#include "config.h"
#include <time.h>
#include <GL/glut.h>
#include <stdio.h>
#include <string.h>

#include "include/GLView.h"
#include "include/Base.h"
#include "include/World.h"
#include "include/queue.h"

void renderString(float x, float y, void *font, const char *string, float r,
                  float g, float b) {
  if (!GLVIEW.draw_text) {
    return;
  }
  glColor3f(r, g, b);
  glRasterPos2f(x, y);
  int32_t len = (int32_t)strlen(string);
  for (int32_t i = 0; i < len; i++)
    glutBitmapCharacter(font, string[i]);
}

void drawCircle(float x, float y, float r) {
  float n;
  for (int32_t k = 0; k < 17; k++) {
    n = k * (M_PI / 8);
    glVertex3f(x + r * sin(n), y + r * cos(n), 0);
  }
}

void init_glview() {
  GLVIEW.paused = 0;
  GLVIEW.draw = 1;
  GLVIEW.skipdraw = 1;
  GLVIEW.drawfood = 1;
  GLVIEW.modcounter = 0;
  GLVIEW.lastUpdate = 0;
  GLVIEW.frames = 0;
  GLVIEW.xtranslate = -WIDTH / 2;
  GLVIEW.ytranslate = -HEIGHT / 2;
  GLVIEW.scalemult = 0.4; // 1.0;
  GLVIEW.downb[0] = 0;
  GLVIEW.downb[1] = 0;
  GLVIEW.downb[2] = 0;
  GLVIEW.mousex = 0;
  GLVIEW.mousey = 0;
  GLVIEW.wwidth = WWIDTH;
  GLVIEW.wheight = WHEIGHT;
  GLVIEW.draw_text = 1;
}

void gl_changeSize(int32_t w, int32_t h) {
  // Reset the coordinate system before modifying
  glViewport(0, 0, w, h);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, w, h, 0, 0, 1);
  GLVIEW.wwidth = w;
  GLVIEW.wheight = h;
}

void gl_processMouse(int32_t button, int32_t state, int32_t x, int32_t y) {
  // printf("MOUSE EVENT: button=%i state=%i x=%i y=%i\n", button, state, x, y);

  // have world deal with it. First translate to world coordinates though
  if (button == 0) {
    int32_t wx = (int32_t)((x - GLVIEW.wwidth / 2) / GLVIEW.scalemult) - GLVIEW.xtranslate;
    int32_t wy = (int32_t)((y - GLVIEW.wheight / 2) / GLVIEW.scalemult) - GLVIEW.ytranslate;
    world_processMouse(GLVIEW.base->world, button, state, wx, wy);
  }

  // Scroll up
  if (button == 3) {
    GLVIEW.scalemult += GLVIEW.scalemult * 0.05;
  }

  // Scroll down
  if (button == 4) {
    GLVIEW.scalemult -= GLVIEW.scalemult * 0.05;
  }

  if (GLVIEW.scalemult < 0.01)
    GLVIEW.scalemult = 0.01;

  GLVIEW.mousex = x;
  GLVIEW.mousey = y;
  GLVIEW.downb[button] = 1 - state; // state is backwards, ah well
}

void gl_processMouseActiveMotion(int32_t x, int32_t y) {
  // printf("MOUSE MOTION x=%i y=%i, %i %i %i\n", x, y, downb[0], downb[1],
  // downb[2]);

  if (GLVIEW.downb[1] == 1) {
    // mouse wheel. Change scale
    GLVIEW.scalemult -= 0.005 * (y - GLVIEW.mousey) * GLVIEW.scalemult;
    if (GLVIEW.scalemult < 0.01)
      GLVIEW.scalemult = 0.01;
  }

  if (GLVIEW.downb[2] == 1) {
    // right mouse button. Pan around
    GLVIEW.xtranslate += (x - GLVIEW.mousex) / GLVIEW.scalemult;
    GLVIEW.ytranslate += (y - GLVIEW.mousey) / GLVIEW.scalemult;
  }

  // printf("%f %f %f \n", scalemult, xtranslate, ytranslate);

  GLVIEW.mousex = x;
  GLVIEW.mousey = y;
}

void gl_processNormalKeys(unsigned char key, int32_t __x, int32_t __y) {
  switch (key) {
    case 27:
      printf("\n\nESC key pressed, shutting down\n");
      queue_close(GLVIEW.base->world->agent_queue);
      base_saveworld(GLVIEW.base);
      avec_free(&GLVIEW.base->world->agents);
      avec_free(&GLVIEW.base->world->agents_staging);
      exit(0);
      break;
    case 'r':
      world_reset(GLVIEW.base->world);
      printf("Agents reset\n");
      break;
    case 'p':
      GLVIEW.paused = !GLVIEW.paused;
      break;
    case 'd':
      GLVIEW.draw = !GLVIEW.draw;
      break;
    case '=':
    case '+':
      GLVIEW.skipdraw++;
      break;
    case '-':
      GLVIEW.skipdraw--;
      break;
    case 'f':
      GLVIEW.drawfood = !GLVIEW.drawfood;
      break;
    case 'a':
      for (int32_t i = 0; i < 10; i++) {
        world_addNewByCrossover(GLVIEW.base->world);
      }
      break;
    case 'q':
      for (int32_t i = 0; i < 10; i++) {
        world_addCarnivore(GLVIEW.base->world);
      }
      break;
    case 'c':
      GLVIEW.base->world->closed = GLVIEW.base->world->closed ? 0 : 1;
      printf("Environemt closed now= %i\n", GLVIEW.base->world->closed);
      break;
    case 'z':
      GLVIEW.xtranslate = -WIDTH / 2;
      GLVIEW.ytranslate = -HEIGHT / 2;
      GLVIEW.scalemult = 0.4; // 1.0;
      break;
    case 't':
      GLVIEW.draw_text = GLVIEW.draw_text ? 0 : 1;
      break;
    // C-l
    case 12:
      base_loadworld(GLVIEW.base);
      break;
    // C-s
    case 19:
      base_saveworld(GLVIEW.base);
      break;
    // C-f
    case 6:
      glview_toggleFullscreen();
      printf("Toggling full screen\n");
      break;
    default:
      printf("Unknown key pressed: %i\n", key);
  }
}

void gl_handleIdle() {
  GLVIEW.modcounter++;
  if (!GLVIEW.paused)
    world_update(GLVIEW.base->world);

  // show FPS
  int32_t currentTime = glutGet(GLUT_ELAPSED_TIME);
  GLVIEW.frames++;
  if ((currentTime - GLVIEW.lastUpdate) >= 1000) {
    int32_t num_herbs = world_numHerbivores(GLVIEW.base->world);
    int32_t num_carns = world_numCarnivores(GLVIEW.base->world);
    sprintf(GLVIEW.buf, "FPS: %d NumAgents: %d Carnivores: %d Herbivores: %d Epoch: %d",
            GLVIEW.frames, world_numAgents(GLVIEW.base->world), num_carns,
            num_herbs, GLVIEW.base->world->current_epoch);
    glutSetWindowTitle(GLVIEW.buf);
    GLVIEW.frames = 0;
    GLVIEW.lastUpdate = currentTime;
  }
  if (GLVIEW.skipdraw <= 0 && GLVIEW.draw) {
    clock_t endwait;
    float mult = -0.005 * (GLVIEW.skipdraw - 1); // ugly, ah well
    endwait = clock() + mult * CLOCKS_PER_SEC;
    while (clock() < endwait) {
    }
  }

  if (GLVIEW.draw) {
    if (GLVIEW.skipdraw > 0) {
      if (GLVIEW.modcounter % GLVIEW.skipdraw == 0)
        gl_renderScene(); // increase fps by skipping drawing
    } else
      gl_renderScene(); // we will decrease fps by waiting using clocks
  }
}

void gl_renderScene() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glPushMatrix();

  glTranslatef(GLVIEW.wwidth / 2, GLVIEW.wheight / 2, 0.0f);
  glScalef(GLVIEW.scalemult, GLVIEW.scalemult, 1.0f);
  glTranslatef(GLVIEW.xtranslate, GLVIEW.ytranslate, 0.0f);

  glview_draw(GLVIEW.base->world, GLVIEW.drawfood);

  glPopMatrix();
  glutSwapBuffers();
}

void drawAgent(const struct Agent *agent) {
  // Determine if an agent is off screen, give some wiggle room
  float asx = (agent->pos.x + GLVIEW.xtranslate) * (GLVIEW.scalemult);
  float asy = (agent->pos.y + GLVIEW.ytranslate) * (GLVIEW.scalemult);

  if ((agent->selectflag == 0) && (asx > GLVIEW.wwidth * 1.1f || asx < -GLVIEW.wwidth * 1.1f ||
                                  asy > GLVIEW.wheight * 1.1f || asy < -GLVIEW.wheight * 1.1f)) {
    return;
  }

  float n;
  float r = BOTRADIUS;
  float rp = BOTRADIUS + 2;

  // handle selected agent
  if (agent->selectflag > 0) {

    // lerp to the target
    GLVIEW.xtranslate += (-agent->pos.x - GLVIEW.xtranslate) * 0.05f;
    GLVIEW.ytranslate += (-agent->pos.y - GLVIEW.ytranslate) * 0.05f;

    // draw selection
    glBegin(GL_POLYGON);
    glColor3f(1, 1, 0);
    drawCircle(agent->pos.x, agent->pos.y, BOTRADIUS + 5);
    glEnd();

    glPushMatrix();
    glViewport(0, 0, GLVIEW.wwidth, GLVIEW.wheight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, GLVIEW.wwidth, GLVIEW.wheight, 0, 0, 1);
    glTranslatef(10, 10, 0);
    // draw inputs, outputs
    float col;
    float yy = 15;
    float xx = 15;
    float ss = 16;
    glBegin(GL_QUADS);
    for (int32_t j = 0; j < INPUTSIZE; j++) {
      col = agent->in[j];
      glColor3f(col, col, col);
      glVertex3f(0 + ss * j, 0, 0.0f);
      glVertex3f(xx + ss * j, 0, 0.0f);
      glVertex3f(xx + ss * j, yy, 0.0f);
      glVertex3f(0 + ss * j, yy, 0.0f);
    }
    yy += 5;
    for (int32_t j = 0; j < OUTPUTSIZE; j++) {
      col = agent->out[j];
      glColor3f(col, col, col);
      glVertex3f(0 + ss * j, yy, 0.0f);
      glVertex3f(xx + ss * j, yy, 0.0f);
      glVertex3f(xx + ss * j, yy + ss, 0.0f);
      glVertex3f(0 + ss * j, yy + ss, 0.0f);
    }
    yy += ss * 2;

    // draw brain. Eventually move this to brain class?
    float offx = 0;
    ss = 8;
    for (int32_t j = 0; j < BRAINSIZE; j++) {
      col = agent->brain.boxes[j].out;
      glColor3f(col, col, col);
      glVertex3f(offx + 0 + ss * j, yy, 0.0f);
      glVertex3f(offx + xx + ss * j, yy, 0.0f);
      glVertex3f(offx + xx + ss * j, yy + ss, 0.0f);
      glVertex3f(offx + ss * j, yy + ss, 0.0f);
      if ((j + 1) % 40 == 0) {
        yy += ss;
        offx -= ss * 40;
      }
    }

    glEnd();
    glPopMatrix();
  }

  // draw giving/receiving
  if (agent->dfood != 0) {
    glBegin(GL_POLYGON);
    float mag = cap(fabsf(agent->dfood) / FOODTRANSFER / 3);
    if (agent->dfood > 0)
      glColor3f(0, mag, 0); // draw boost as green outline
    else
      glColor3f(mag, 0, 0);
    for (int32_t k = 0; k < 17; k++) {
      n = k * (M_PI / 8);
      glVertex3f(agent->pos.x + rp * sin(n), agent->pos.y + rp * cos(n), 0);
      n = (k + 1) * (M_PI / 8);
      glVertex3f(agent->pos.x + rp * sin(n), agent->pos.y + rp * cos(n), 0);
    }
    glEnd();
  }

  // draw indicator of this agent->.. used for various events
  if (agent->indicator > 0) {
    glBegin(GL_POLYGON);
    glColor3f(agent->ir, agent->ig, agent->ib);
    drawCircle(agent->pos.x, agent->pos.y,
               BOTRADIUS + ((int32_t)agent->indicator));
    glEnd();
  }

  // viewcone of this agent
  glBegin(GL_LINES);
  // and view cones
  glColor3f(0.5, 0.5, 0.5);
  for (int32_t j = -2; j < 3; j++) {
    if (j == 0)
      continue;
    glVertex3f(agent->pos.x, agent->pos.y, 0);
    glVertex3f(
        agent->pos.x + (BOTRADIUS * 4) * cos(agent->angle + j * M_PI / 8),
        agent->pos.y + (BOTRADIUS * 4) * sin(agent->angle + j * M_PI / 8),
        0);
  }
  // and eye to the back
  glVertex3f(agent->pos.x, agent->pos.y, 0);
  glVertex3f(agent->pos.x + (BOTRADIUS * 1.5) *
                               cos(agent->angle + M_PI + 3 * M_PI / 16),
             agent->pos.y + (BOTRADIUS * 1.5) *
                               sin(agent->angle + M_PI + 3 * M_PI / 16),
             0);
  glVertex3f(agent->pos.x, agent->pos.y, 0);
  glVertex3f(agent->pos.x + (BOTRADIUS * 1.5) *
                               cos(agent->angle + M_PI - 3 * M_PI / 16),
             agent->pos.y + (BOTRADIUS * 1.5) *
                               sin(agent->angle + M_PI - 3 * M_PI / 16),
             0);
  glEnd();

  glBegin(GL_POLYGON); // body
  glColor3f(agent->red, agent->gre, agent->blu);
  drawCircle(agent->pos.x, agent->pos.y, BOTRADIUS);
  glEnd();

  glBegin(GL_LINES);
  // outline
  if (agent->boost)
    glColor3f(0.8, 0, 0); // draw boost as green outline
  else
    glColor3f(0, 0, 0);

  for (int32_t k = 0; k < 17; k++) {
    n = k * (M_PI / 8);
    glVertex3f(agent->pos.x + r * sin(n), agent->pos.y + r * cos(n), 0);
    n = (k + 1) * (M_PI / 8);
    glVertex3f(agent->pos.x + r * sin(n), agent->pos.y + r * cos(n), 0);
  }
  // and spike
  glColor3f(0.5, 0, 0);
  glVertex3f(agent->pos.x, agent->pos.y, 0);
  glVertex3f(agent->pos.x + (3 * r * agent->spikeLength) * cos(agent->angle),
             agent->pos.y + (3 * r * agent->spikeLength) * sin(agent->angle), 0);
  glEnd();

  // and health
  int32_t xo = 18;
  int32_t yo = -15;
  glBegin(GL_QUADS);
  // black background
  glColor3f(0, 0, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo, 0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo, 0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo + 40, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo + 40, 0);

  // health
  glColor3f(0, 0.8, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo + 20 * (2 - agent->health), 0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo + 20 * (2 - agent->health),
             0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo + 40, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo + 40, 0);

  // if this is a hybrid, we want to put a marker down
  if (agent->hybrid) {
    glColor3f(0, 0, 0.8);
    glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo, 0);
    glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo, 0);
    glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 10, 0);
    glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 10, 0);
  }

  glColor3f(1 - agent->herbivore, agent->herbivore, 0);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 12, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 12, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 22, 0);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 22, 0);

  // how much sound is this bot making?
  glColor3f(agent->soundmul, agent->soundmul, agent->soundmul);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 24, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 24, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 34, 0);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 34, 0);

  // draw giving/receiving
  if (agent->dfood != 0) {

    float mag = cap(fabsf(agent->dfood) / FOODTRANSFER / 3);
    if (agent->dfood > 0)
      glColor3f(0, mag, 0); // draw boost as green outline
    else
      glColor3f(mag, 0, 0);
    glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 36, 0);
    glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 36, 0);
    glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 46, 0);
    glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 46, 0);
  }

  glEnd();

  // print stats if zoomed in enough
  if (GLVIEW.scalemult > .7) {
    // generation count
    sprintf(GLVIEW.buf2, "%i", agent->gencount);
    renderString(agent->pos.x - BOTRADIUS * 1.5,
                 agent->pos.y + BOTRADIUS * 1.8,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 0.0f, 0.0f, 0.0f);
    // age
    sprintf(GLVIEW.buf2, "%i", agent->age);
    renderString(agent->pos.x - BOTRADIUS * 1.5,
                 agent->pos.y + BOTRADIUS * 1.8 + 12,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 0.0f, 0.0f, 0.0f);

    // health
    sprintf(GLVIEW.buf2, "%.2f", agent->health);
    renderString(agent->pos.x - BOTRADIUS * 1.5,
                 agent->pos.y + BOTRADIUS * 1.8 + 24,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 0.0f, 0.0f, 0.0f);

    // repcounter
    sprintf(GLVIEW.buf2, "%.2f", agent->repcounter);
    renderString(agent->pos.x - BOTRADIUS * 1.5,
                 agent->pos.y + BOTRADIUS * 1.8 + 36,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 0.0f, 0.0f, 0.0f);
  }
}

void drawFood(int32_t x, int32_t y, float quantity) {
  // draw food
  if (GLVIEW.drawfood) {
    glBegin(GL_QUADS);
    glColor3f(0.9 - quantity, 0.9 - quantity, 1.0 - quantity);
    glVertex3f(x * CZ, y * CZ, 0);
    glVertex3f(x * CZ + CZ, y * CZ, 0);
    glVertex3f(x * CZ + CZ, y * CZ + CZ, 0);
    glVertex3f(x * CZ, y * CZ + CZ, 0);
    glEnd();
  }
}

void glview_draw(struct World *world, int32_t drawfood) {
  if (drawfood) {
    for (int32_t i = 0; i < world->FW; i++) {
      for (int32_t j = 0; j < world->FH; j++) {
        float f = 0.5 * world->food[i][j] / FOODMAX;
        drawFood(i, j, f);
      }
    }
  }
  for (size_t i = 0; i < world->agents.size; i++) {
    drawAgent(&world->agents.agents[i]);
  }
}

void glview_toggleFullscreen() {
  if (GLVIEW.is_fullscreen) {
    glutReshapeWindow(GLVIEW.prev_width, GLVIEW.prev_height);
  } else {
    GLVIEW.prev_width = GLVIEW.wwidth;
    GLVIEW.prev_height = GLVIEW.wheight;
    glutFullScreen();
  }
  GLVIEW.is_fullscreen = !GLVIEW.is_fullscreen;
}
