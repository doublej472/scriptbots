#include <GL/freeglut.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Base.h"
#include "GLView.h"
#include "World.h"
#include "queue.h"

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
    n = k * ((float)M_PI / 8.0f);
    glVertex3f(x + r * sinf(n), y + r * cosf(n), 0);
  }
}

void init_glview(int32_t argc, char **argv) {
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

  // GLUT SETUP
  glutInit(&argc, argv);
  glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
  glutInitContextVersion(2, 1);
  // glutInitContextFlags(GLUT_FORWARD_COMPATIBLE);
  // glutInitContextProfile(GLUT_CORE_PROFILE);
  glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
  glutInitWindowSize(WWIDTH, WHEIGHT);
  glutCreateWindow("Scriptbots");
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glutDisplayFunc(gl_renderScene);
  glutIdleFunc(gl_handleIdle);
  glutReshapeFunc(gl_changeSize);

  glutKeyboardFunc(gl_processNormalKeys);
  glutSpecialFunc(gl_processSpecialKeys);
  glutMouseFunc(gl_processMouse);
  glutMotionFunc(gl_processMouseActiveMotion);
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
    int32_t wx = (int32_t)((x - GLVIEW.wwidth / 2.0f) / GLVIEW.scalemult) -
                 GLVIEW.xtranslate;
    int32_t wy = (int32_t)((y - GLVIEW.wheight / 2.0f) / GLVIEW.scalemult) -
                 GLVIEW.ytranslate;
    world_processMouse(GLVIEW.base->world, button, state, wx, wy);
  }

  // Scroll up
  if (button == 3) {
    GLVIEW.scalemult += GLVIEW.scalemult * 0.05f;
  }

  // Scroll down
  if (button == 4) {
    GLVIEW.scalemult -= GLVIEW.scalemult * 0.05f;
  }

  if (GLVIEW.scalemult < 0.01f)
    GLVIEW.scalemult = 0.01f;

  GLVIEW.mousex = x;
  GLVIEW.mousey = y;
  GLVIEW.downb[button] = 1 - state; // state is backwards, ah well
}

void gl_processMouseActiveMotion(int32_t x, int32_t y) {
  // printf("MOUSE MOTION x=%i y=%i, %i %i %i\n", x, y, downb[0], downb[1],
  // downb[2]);

  if (GLVIEW.downb[1] == 1) {
    // mouse wheel. Change scale
    GLVIEW.scalemult -= 0.005f * (y - GLVIEW.mousey) * GLVIEW.scalemult;
    if (GLVIEW.scalemult < 0.01f)
      GLVIEW.scalemult = 0.01f;
  }

  if (GLVIEW.downb[2] == 1) {
    // right mouse button. Pan around
    GLVIEW.xtranslate += (x - GLVIEW.mousex) / GLVIEW.scalemult;
    GLVIEW.ytranslate += (y - GLVIEW.mousey) / GLVIEW.scalemult;
  }

  // printf("%f %f %f \n", GLVIEW.scalemult, GLVIEW.xtranslate,
  // GLVIEW.ytranslate);

  GLVIEW.mousex = x;
  GLVIEW.mousey = y;
}

void gl_processSpecialKeys(int key, int x, int y) {
  switch (key) {
  case GLUT_KEY_PAGE_UP:
    GLVIEW.scalemult += GLVIEW.scalemult * 0.05f;
    break;
  case GLUT_KEY_PAGE_DOWN:
    GLVIEW.scalemult -= GLVIEW.scalemult * 0.05f;
    if (GLVIEW.scalemult < 0.01f) {
      GLVIEW.scalemult = 0.01f;
    }
    break;
  case GLUT_KEY_UP:
    GLVIEW.ytranslate += 5.0f / GLVIEW.scalemult;
    break;
  case GLUT_KEY_DOWN:
    GLVIEW.ytranslate -= 5.0f / GLVIEW.scalemult;
    break;
  case GLUT_KEY_RIGHT:
    GLVIEW.xtranslate -= 5.0f / GLVIEW.scalemult;
    break;
  case GLUT_KEY_LEFT:
    GLVIEW.xtranslate += 5.0f / GLVIEW.scalemult;
    break;
  default:
    printf("Unknown special key pressed: %i\n", key);
  }
}

void gl_processNormalKeys(unsigned char key, int32_t __x, int32_t __y) {
  switch (key) {
  case 27:
    printf("\n\nESC key pressed, shutting down\n");
    glutLeaveMainLoop();
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
  case 'h':
    world_addRandomBots(GLVIEW.base->world, 100);
    break;
  case 'q':
    for (int32_t i = 0; i < 100; i++) {
      world_addCarnivore(GLVIEW.base->world);
    }
    break;
  case 'c':
    GLVIEW.base->world->closed = GLVIEW.base->world->closed ? 0 : 1;
    printf("Environemt closed now= %i\n", GLVIEW.base->world->closed);
    break;
  case 'z':
    GLVIEW.xtranslate = -WIDTH / 2.0f;
    GLVIEW.ytranslate = -HEIGHT / 2.0f;
    GLVIEW.scalemult = 0.4f; // 1.0;
    break;
  case 't':
    GLVIEW.draw_text = GLVIEW.draw_text ? 0 : 1;
    break;
  case 'm':
    GLVIEW.base->world->movieMode = !GLVIEW.base->world->movieMode;
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

static const int MILLS_PER_UPDATE = 250;

void gl_handleIdle() {
  GLVIEW.modcounter++;
  if (!GLVIEW.paused)
    world_update(GLVIEW.base->world);

  // show FPS
  int32_t currentTime = glutGet(GLUT_ELAPSED_TIME);
  GLVIEW.frames++;
  if ((currentTime - GLVIEW.lastUpdate) >= MILLS_PER_UPDATE) {
    int32_t num_herbs = world_numHerbivores(GLVIEW.base->world);
    int32_t num_carns = world_numCarnivores(GLVIEW.base->world);
    sprintf(GLVIEW.buf,
            "FPS: %.2f NumAgents: %d Carnivores: %d Herbivores: %d Epoch: %d",
            GLVIEW.frames * (1000.0 / MILLS_PER_UPDATE),
            world_numAgents(GLVIEW.base->world), num_carns, num_herbs,
            GLVIEW.base->world->current_epoch);
    glutSetWindowTitle(GLVIEW.buf);
    GLVIEW.frames = 0;
    GLVIEW.lastUpdate = currentTime;
  }
  if (GLVIEW.skipdraw <= 0 && GLVIEW.draw) {
    clock_t endwait;
    float mult = -0.005f * (GLVIEW.skipdraw - 1); // ugly, ah well
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
  // glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glPushMatrix();

  glTranslatef(GLVIEW.wwidth / 2.0f, GLVIEW.wheight / 2.0f, 0.0f);
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

  // Basic culling
  if ((agent->selectflag == 0) &&
      (asx > GLVIEW.wwidth * 1.1f || asx < -GLVIEW.wwidth * 1.1f ||
       asy > GLVIEW.wheight * 1.1f || asy < -GLVIEW.wheight * 1.1f)) {
    return;
  }

  float n;
  float r = BOTRADIUS;

  // handle selected agent
  if (agent->selectflag > 0) {

    // lerp to the target
    GLVIEW.xtranslate += (-agent->pos.x - GLVIEW.xtranslate) * 0.05f;
    GLVIEW.ytranslate += (-agent->pos.y - GLVIEW.ytranslate) * 0.05f;

    // draw selection
    glBegin(GL_POLYGON);
    glColor3f(1, 1, 0);
    drawCircle(agent->pos.x, agent->pos.y, BOTRADIUS + 5.0f);
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
      if (j < 18) {
        glColor3f(col, col, col);
      } else if (j < 19) {
        // random inputs
        glColor3f(0.0f, col, col);
      } else {
        // plan inputs
        glColor3f(0.0f, col, 0.0f);
      }
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
    ss = 8;

    for (int32_t j = 0; j < BRAIN_DEPTH; j++) {
      int offy = j;
      for (int32_t k = 0; k < BRAIN_WIDTH; k++) {
        int32_t ng = k / 8;
        int32_t elem = k % 8;

        float col = agent->brain->layers[j].inputs[ng][elem];

        int offx = k;

        glColor3f(col, col, col);
        glVertex3f(ss * offx, yy + ss * offy, 0.0f);
        glVertex3f(ss * offx + (ss), yy + ss * offy, 0.0f);
        glVertex3f(ss * offx + (ss), yy + ss * offy + ss, 0.0f);
        glVertex3f(ss * offx, yy + ss * offy + ss, 0.0f);
      }
    }
    glEnd();

    yy += (ss * (BRAIN_DEPTH + 1) + 14);

    sprintf(GLVIEW.buf2, "Health: %f", agent->health);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Pos: %f\t%f", agent->pos.x, agent->pos.y);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Angle: %f", agent->angle);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Children: %i", agent->numchildren);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Generation: %jd", agent->gencount);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Age: %i", agent->age);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Repcounter: %f", agent->repcounter);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Herbivore: %f", agent->herbivore);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Mutate Rate Chance: %f", agent->MUTRATE1);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Mutate Rate Magnitude: %f", agent->MUTRATE2);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);
    yy += 14;
    sprintf(GLVIEW.buf2, "Wheel speeds (L, R): %f %f", agent->w2, agent->w1);
    renderString(0, yy, GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f,
                 1.0f);

    glPopMatrix();
  }

  // draw indicator of this agent->.. used for various events
  if (agent->indicator > 0) {
    glBegin(GL_POLYGON);
    glColor3f(agent->ir, agent->ig, agent->ib);
    drawCircle(agent->pos.x, agent->pos.y,
               (float)BOTRADIUS + ((int32_t)agent->indicator));
    glEnd();
  }

  // viewcone of this agent
  glBegin(GL_LINES);
  // and view cones
  glColor3f(0.5, 0.5, 0.5);
  for (int32_t j = -2; j <= 2; j++) {
    if (j == 0)
      continue;
    glVertex3f(agent->pos.x, agent->pos.y, 0);
    glVertex3f(agent->pos.x + (BOTRADIUS * 4.0f) *
                                  cosf(agent->angle + j * (float)M_PI / 8.0f),
               agent->pos.y + (BOTRADIUS * 4.0f) *
                                  sinf(agent->angle + j * (float)M_PI / 8.0f),
               0);
  }
  glEnd();

  glBegin(GL_POLYGON); // body
  glColor3f(agent->red, agent->gre, agent->blu);
  drawCircle(agent->pos.x, agent->pos.y, BOTRADIUS);
  glEnd();

  glBegin(GL_LINES);
  // outline
  if (agent->boost)
    glColor3f(0.8f, 0, 0); // draw boost as green outline
  else
    glColor3f(0, 0, 0);

  for (int32_t k = 0; k < 17; k++) {
    n = k * ((float)M_PI / 8.0f);
    glVertex3f(agent->pos.x + r * sinf(n), agent->pos.y + r * cosf(n), 0);
    n = (k + 1.0f) * ((float)M_PI / 8.0f);
    glVertex3f(agent->pos.x + r * sinf(n), agent->pos.y + r * cosf(n), 0);
  }
  // and spike
  glColor3f(0.5, 0, 0);
  glVertex3f(agent->pos.x, agent->pos.y, 0);
  glVertex3f(
      agent->pos.x + (3.0f * r * agent->spikeLength) * cosf(agent->angle),
      agent->pos.y + (3.0f * r * agent->spikeLength) * sinf(agent->angle), 0);
  glEnd();

  // and health
  int32_t xo = 18;
  int32_t yo = -15;
  glBegin(GL_QUADS);
  // red background
  glColor3f(0.5, 0.0, 0.0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo, 0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo, 0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo + 40, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo + 40, 0);

  // health
  glColor3f(0, 0.8, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo + 20 * (2 - agent->health),
             0);
  glVertex3f(agent->pos.x + xo + 5,
             agent->pos.y + yo + 20 * (2 - agent->health), 0);
  glVertex3f(agent->pos.x + xo + 5, agent->pos.y + yo + 40, 0);
  glVertex3f(agent->pos.x + xo, agent->pos.y + yo + 40, 0);

  glColor3f(1 - agent->herbivore, agent->herbivore, 0);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 0, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 0, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 10, 0);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 10, 0);

  // how much sound is this bot making?
  glColor3f(agent->soundmul, agent->soundmul, agent->soundmul);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 12, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 12, 0);
  glVertex3f(agent->pos.x + xo + 12, agent->pos.y + yo + 22, 0);
  glVertex3f(agent->pos.x + xo + 6, agent->pos.y + yo + 22, 0);

  glEnd();

  // print stats if zoomed in enough
  if (GLVIEW.scalemult > 0.7f) {
    // generation count
    sprintf(GLVIEW.buf2, "%jd", agent->gencount);
    renderString(agent->pos.x - BOTRADIUS * 2,
                 agent->pos.y + 5.0f + BOTRADIUS * 2, GLUT_BITMAP_HELVETICA_12,
                 GLVIEW.buf2, 1.0f, 1.0f, 1.0f);
    // age
    sprintf(GLVIEW.buf2, "%i", agent->age);
    renderString(agent->pos.x - BOTRADIUS * 2,
                 agent->pos.y + 5.0f + BOTRADIUS * 2 + 12,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f, 1.0f);

    // health
    sprintf(GLVIEW.buf2, "%.2f", agent->health);
    renderString(agent->pos.x - BOTRADIUS * 2,
                 agent->pos.y + 5.0f + BOTRADIUS * 2 + 24,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f, 1.0f);

    // repcounter
    sprintf(GLVIEW.buf2, "%.2f", agent->repcounter);
    renderString(agent->pos.x - BOTRADIUS * 2,
                 agent->pos.y + 5.0f + BOTRADIUS * 2 + 36,
                 GLUT_BITMAP_HELVETICA_12, GLVIEW.buf2, 1.0f, 1.0f, 1.0f);
  }
}

void glview_draw(struct World *world, int32_t drawfood) {
  glBegin(GL_QUADS);
  glColor3f(1.0f, 0.5f, 0.0f);
  // Left wall
  glVertex3f(-5.0f, -5.0f, 0);
  glVertex3f(0.0f, -5.0f, 0);
  glVertex3f(0.0f, HEIGHT + 5.0f, 0);
  glVertex3f(-5.0f, HEIGHT + 5.0f, 0);
  // Right wall
  glVertex3f(WIDTH + 5.0f, -5.0f, 0);
  glVertex3f(WIDTH, -5.0f, 0);
  glVertex3f(WIDTH, HEIGHT + 5.0f, 0);
  glVertex3f(WIDTH + 5.0f, HEIGHT + 5.0f, 0);
  // Top wall
  glVertex3f(0.0f, -5.0f, 0);
  glVertex3f(0.0f, 0.0f, 0);
  glVertex3f(WIDTH, 0.0f, 0);
  glVertex3f(WIDTH, -5.0f, 0);
  // Bottom wall
  glVertex3f(0.0f, HEIGHT + 5.0f, 0);
  glVertex3f(0.0f, HEIGHT, 0);
  glVertex3f(WIDTH, HEIGHT, 0);
  glVertex3f(WIDTH, HEIGHT + 5.0f, 0);
  glEnd();

  if (drawfood) {
    glBegin(GL_QUADS);
    for (int32_t i = 0; i < world->FW; i++) {
      for (int32_t j = 0; j < world->FH; j++) {
        // Determine if food is off screen, give some wiggle room
        float asx = ((i * CZ) + GLVIEW.xtranslate) * (GLVIEW.scalemult);
        float asy = ((j * CZ) + GLVIEW.ytranslate) * (GLVIEW.scalemult);

        // Basic culling
        if (asx > GLVIEW.wwidth + CZ * 2.0f ||
            asx < -(GLVIEW.wwidth + CZ * 2.0f) ||
            asy > GLVIEW.wheight + CZ * 2.0f ||
            asy < -(GLVIEW.wheight + CZ * 2.0f)) {
          continue;
        }

        float f = world->food[i][j] / FOODMAX;

        if (GLVIEW.drawfood && f > 0.00001f) {
          glColor3f(0.02f, 0.02f + f * 0.8f, 0.02f);
          glVertex3f(i * CZ, j * CZ, 0);
          glVertex3f(i * CZ + CZ, j * CZ, 0);
          glVertex3f(i * CZ + CZ, j * CZ + CZ, 0);
          glVertex3f(i * CZ, j * CZ + CZ, 0);
        }
      }
    }
    glEnd();
  }
  for (size_t i = 0; i < world->agents.size; i++) {
    drawAgent(world->agents.agents[i]);
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
