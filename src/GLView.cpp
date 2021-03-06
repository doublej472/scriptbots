#include "include/GLView.h"
#include "config.h"
#include <cstdio>
#include <ctime>
#ifdef MAC_GLUT
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

void gl_processNormalKeys(unsigned char key, int x, int y) {
  GLVIEW->processNormalKeys(key, x, y);
}
void gl_changeSize(int w, int h) { GLVIEW->changeSize(w, h); }
void gl_handleIdle() { GLVIEW->handleIdle(); }
void gl_processMouse(int button, int state, int x, int y) {
  GLVIEW->processMouse(button, state, x, y);
}
void gl_processMouseActiveMotion(int x, int y) {
  GLVIEW->processMouseActiveMotion(x, y);
}
void gl_renderScene() { GLVIEW->renderScene(); }

void RenderString(float x, float y, void *font, const char *string, float r,
                  float g, float b) {
  glColor3f(r, g, b);
  glRasterPos2f(x, y);
  int len = (int)strlen(string);
  for (int i = 0; i < len; i++)
    glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, string[i]);
}

void drawCircle(float x, float y, float r) {
  float n;
  for (int k = 0; k < 17; k++) {
    n = k * (M_PI / 8);
    glVertex3f(x + r * sin(n), y + r * cos(n), 0);
  }
}

void drawRectangleLines(float x, float y, float width, float height) {
  glVertex3f(x, y, 0.0f);
  glVertex3f(x + width, y, 0.0f);

  glVertex3f(x + width, y, 0.0f);
  glVertex3f(x + width, y + height, 0.0f);

  glVertex3f(x + width, y + height, 0.0f);
  glVertex3f(x, y + height, 0.0f);

  glVertex3f(x, y + height, 0.0f);
  glVertex3f(x, y, 0.0f);
}

// Assumes we are in GL_TRIANGLE
void drawRectangle(float x, float y, float width, float height) {
  glVertex3f(x, y, 0.0f);
  glVertex3f(x + width, y, 0.0f);
  glVertex3f(x, y + height, 0.0f);

  glVertex3f(x + width, y, 0.0f);
  glVertex3f(x, y + height, 0.0f);
  glVertex3f(x + width, y + height, 0.0f);
}

GLView::GLView()
    : // World *s) :
      //        world(world),
      paused(false), draw(true), skipdraw(1), drawfood(true), drawconns(true),
      modcounter(0), lastUpdate(0), frames(0) {

  xtranslate = -conf::WIDTH / 2;
  ytranslate = -conf::HEIGHT / 2;
  scalemult = 0.4; // 1.0;
  downb[0] = 0;
  downb[1] = 0;
  downb[2] = 0;
  mousex = 0;
  mousey = 0;
  wwidth = conf::WWIDTH;
  wheight = conf::WHEIGHT;
  is_fullscreen = false;
  prev_width = wwidth;
  prev_height = wheight;
}

GLView::~GLView() {}

void GLView::toggleFullscreen() {
  if (is_fullscreen) {
    glutReshapeWindow(prev_width, prev_height);
  } else {
    prev_width = wwidth;
    prev_height = wheight;
    glutFullScreen();
  }
  is_fullscreen = !is_fullscreen;
}

void GLView::changeSize(int w, int h) {
  // Reset the coordinate system before modifying
  glViewport(0, 0, w, h);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, w, h, 0, 0, 1);
  wwidth = w;
  wheight = h;
}

void GLView::processMouse(int button, int state, int x, int y) {
  // printf("MOUSE EVENT: button=%i state=%i x=%i y=%i\n", button, state, x, y);

  // have world deal with it. First translate to world coordinates though
  if (button == 0) {
    int wx = (int)((x - wwidth / 2) / scalemult) - xtranslate;
    int wy = (int)((y - wheight / 2) / scalemult) - ytranslate;
    base->world->processMouse(button, state, wx, wy);
  }

  // Scroll up
  if (button == 3) {
    scalemult += scalemult * 0.05;
  }

  // Scroll down
  if (button == 4) {
    scalemult -= scalemult * 0.05;
  }

  if (scalemult < 0.01)
    scalemult = 0.01;

  mousex = x;
  mousey = y;
  downb[button] = 1 - state; // state is backwards, ah well
}

void GLView::processMouseActiveMotion(int x, int y) {
  // printf("MOUSE MOTION x=%i y=%i, %i %i %i\n", x, y, downb[0], downb[1],
  // downb[2]);

  if (downb[1] == 1) {
    // mouse wheel. Change scale
    scalemult -= 0.005 * (y - mousey) * scalemult;
    if (scalemult < 0.01)
      scalemult = 0.01;
  }

  if (downb[2] == 1) {
    // right mouse button. Pan around
    xtranslate += (x - mousex) / scalemult;
    ytranslate += (y - mousey) / scalemult;
  }

  // printf("%f %f %f \n", scalemult, xtranslate, ytranslate);

  mousex = x;
  mousey = y;
}

void GLView::processNormalKeys(unsigned char key, int x, int y) {
  switch (key) {
  case 27:
    printf("\n\nESC key pressed, shutting down\n");
    base->saveWorld();
    exit(0);
    break;
  case 'r':
    base->world->reset();
    printf("Agents reset\n");
    break;
  case 'p':
    paused = !paused;
    break;
  case 'd':
    draw = !draw;
    break;
  case '=':
  case '+':
    skipdraw++;
    break;
  case '-':
    skipdraw--;
    break;
  case 'f':
    drawfood = !drawfood;
    break;
  case 'g':
    drawconns = !drawconns;
    break;
  case 'a':
    for (int i = 0; i < 10; i++) {
      base->world->addNewByCrossover();
    }
    break;
  case 'q':
    for (int i = 0; i < 10; i++) {
      base->world->addCarnivore();
    }
    break;
  case 'c':
    base->world->setClosed(!base->world->isClosed());
    printf("Environemt closed now= %i\n", base->world->isClosed());
    break;
  // C-f
  case 6:
    toggleFullscreen();
    printf("Toggling full screen\n");
    break;
  default:
    printf("Unknown key pressed: %i\n", key);
  }
}

void GLView::handleIdle() {
  modcounter++;
  if (!paused)
    base->world->update();

  // show FPS
  int currentTime = glutGet(GLUT_ELAPSED_TIME);
  frames++;
  if ((currentTime - lastUpdate) >= 1000) {
    std::pair<int, int> num_herbs_carns = base->world->numHerbCarnivores();
    sprintf(buf,
            "FPS: %d NumAgents: %d Carnivores: %d Herbivores: %d Epoch: %d",
            frames, base->world->numAgents(), num_herbs_carns.second,
            num_herbs_carns.first, base->world->epoch());
    glutSetWindowTitle(buf);
    frames = 0;
    lastUpdate = currentTime;
  }
  if (skipdraw <= 0 && draw) {
    clock_t endwait;
    float mult = -0.005 * (skipdraw - 1); // ugly, ah well
    endwait = clock() + mult * CLOCKS_PER_SEC;
    while (clock() < endwait) {
    }
  }

  if (draw) {
    if (skipdraw > 0) {
      if (modcounter % skipdraw == 0)
        renderScene(); // increase fps by skipping drawing
    } else
      renderScene(); // we will decrease fps by waiting using clocks
  }
}

void GLView::renderScene() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glPushMatrix();

  glTranslatef(wwidth / 2, wheight / 2, 0.0f);
  glScalef(scalemult, scalemult, 1.0f);
  glTranslatef(xtranslate, ytranslate, 0.0f);

  base->world->draw(this, drawfood);

  glPopMatrix();
  glutSwapBuffers();
}

void GLView::drawAgent(const Agent &agent) {

  // Determine if an agent is off screen, give some wiggle room
  float asx = (agent.pos.x + xtranslate) * (scalemult);
  float asy = (agent.pos.y + ytranslate) * (scalemult);

  if ((agent.selectflag == 0) && (asx > wwidth * 1.1f || asx < -wwidth * 1.1f ||
                                  asy > wheight * 1.1f || asy < -wheight * 1.1f)) {
    return;
  }

  float n;
  float r = conf::BOTRADIUS;
  float rp = conf::BOTRADIUS + 2;
  // handle selected agent
  if (agent.selectflag > 0) {

    // draw selection
    glBegin(GL_POLYGON);
    glColor3f(1, 1, 0);
    drawCircle(agent.pos.x, agent.pos.y, conf::BOTRADIUS + 5);
    glEnd();

    glPushMatrix();
    glTranslatef(agent.pos.x - 80, agent.pos.y + 20, 0);

    float ss = 8;
    float spacing = 8;
    int brainlength = (int)floor(sqrt(BRAINSIZE));
    float xbrain = (ss + spacing) * 2;
    float xoutput = (3 + brainlength) * (ss + spacing);

    glBegin(GL_TRIANGLES);
    // inputs
    for (int j = 0; j < INPUTSIZE; j++) {
      float color = agent.in[j];
      glColor3f(0.3f, 0.3f, color);
      drawRectangle(0, (ss + spacing) * j, ss, ss);
    }

    // neurons
    for (int j = 0; j < BRAINSIZE; j++) {
      float row = j / brainlength; // y
      float col = j % brainlength; // x
      float color = agent.brain.boxes[j].out;

      glColor3f(color, color, color);
      drawRectangle(col * (ss + spacing) + xbrain, row * (ss + spacing * 3), ss,
                    ss);
    }

    // outputs
    for (int j = 0; j < OUTPUTSIZE; j++) {
      float color = agent.out[j];
      glColor3f(color, 0.3f, 0.3f);
      drawRectangle(xoutput, j * (ss + spacing), ss, ss);
    }
    glEnd();

    // Draw connections
    if (drawconns) {
      glBegin(GL_LINES);
      // inputs
      for (int j = 0; j < INPUTSIZE; j++) {
        float x2 = j % brainlength;
        float y2 = j / brainlength;
        float color = agent.in[j];
        glColor3f(0.0f, 0.0f, color);
        glVertex3f(ss, (ss + spacing) * j + (ss / 2), 0.0f);
        glVertex3f(xbrain + x2 * (ss + spacing),
                   y2 * (ss + spacing * 3) + ss / 2, 0);
      }

      // brain
      for (int j = 0; j < BRAINSIZE; j++) {
        float x1 = j % brainlength;
        float y1 = j / brainlength;
        // For each connection
        for (int k = 0; k < CONNS; k++) {
          float x2 = agent.brain.boxes[j].id[k] % brainlength;
          float y2 = agent.brain.boxes[j].id[k] / brainlength;
          float alpha = std::min(
              std::max(agent.brain.boxes[j].w[k] * agent.brain.boxes[j].out,
                       0.0f),
              1.0f);
          glColor3f(alpha, 0.0f, 0.0f);
          glVertex3f(xbrain + x1 * (ss + spacing) + ss,
                     y1 * (ss + spacing * 3) + ss / 2, 0);
          glColor3f(0.0f, 0.0f, alpha);
          glVertex3f(xbrain + x2 * (ss + spacing),
                     y2 * (ss + spacing * 3) + ss / 2, 0);
        }
      }

      // outputs
      for (int j = 0; j < OUTPUTSIZE; j++) {
        float x1 = (j + BRAINSIZE - OUTPUTSIZE) % brainlength;
        float y1 = (j + BRAINSIZE - OUTPUTSIZE) / brainlength;
        float color = agent.out[j];

        glColor3f(color, 0.0f, 0.0f);
        glVertex3f(xbrain + x1 * (ss + spacing) + ss,
                   y1 * (ss + spacing * 3) + ss / 2, 0.0f);
        glVertex3f(xoutput, j * (ss + spacing) + (ss / 2), 0.0f);
      }
      glEnd();
    }
    glPopMatrix();
  }

  // draw giving/receiving
  if (agent.dfood != 0) {
    glBegin(GL_POLYGON);
    float mag = cap(abs(agent.dfood) / conf::FOODTRANSFER / 3);
    if (agent.dfood > 0)
      glColor3f(0, mag, 0); // draw boost as green outline
    else
      glColor3f(mag, 0, 0);
    for (int k = 0; k < 17; k++) {
      n = k * (M_PI / 8);
      glVertex3f(agent.pos.x + rp * sin(n), agent.pos.y + rp * cos(n), 0);
      n = (k + 1) * (M_PI / 8);
      glVertex3f(agent.pos.x + rp * sin(n), agent.pos.y + rp * cos(n), 0);
    }
    glEnd();
  }

  // draw indicator of this agent... used for various events
  if (agent.indicator > 0) {
    glBegin(GL_POLYGON);
    glColor3f(agent.ir, agent.ig, agent.ib);
    drawCircle(agent.pos.x, agent.pos.y,
               conf::BOTRADIUS + ((int)agent.indicator));
    glEnd();
  }

  // viewcone of this agent
  glBegin(GL_LINES);
  // and view cones
  glColor3f(0.5, 0.5, 0.5);
  for (int j = -2; j < 3; j++) {
    if (j == 0)
      continue;
    glVertex3f(agent.pos.x, agent.pos.y, 0);
    glVertex3f(
        agent.pos.x + (conf::BOTRADIUS * 4) * cos(agent.angle + j * M_PI / 8),
        agent.pos.y + (conf::BOTRADIUS * 4) * sin(agent.angle + j * M_PI / 8),
        0);
  }
  // and eye to the back
  glVertex3f(agent.pos.x, agent.pos.y, 0);
  glVertex3f(agent.pos.x + (conf::BOTRADIUS * 1.5) *
                               cos(agent.angle + M_PI + 3 * M_PI / 16),
             agent.pos.y + (conf::BOTRADIUS * 1.5) *
                               sin(agent.angle + M_PI + 3 * M_PI / 16),
             0);
  glVertex3f(agent.pos.x, agent.pos.y, 0);
  glVertex3f(agent.pos.x + (conf::BOTRADIUS * 1.5) *
                               cos(agent.angle + M_PI - 3 * M_PI / 16),
             agent.pos.y + (conf::BOTRADIUS * 1.5) *
                               sin(agent.angle + M_PI - 3 * M_PI / 16),
             0);
  glEnd();

  glBegin(GL_POLYGON); // body
  glColor3f(agent.red, agent.gre, agent.blu);
  drawCircle(agent.pos.x, agent.pos.y, conf::BOTRADIUS);
  glEnd();

  glBegin(GL_LINES);
  // outline
  if (agent.boost)
    glColor3f(0.8, 0, 0); // draw boost as green outline
  else
    glColor3f(0, 0, 0);

  for (int k = 0; k < 17; k++) {
    n = k * (M_PI / 8);
    glVertex3f(agent.pos.x + r * sin(n), agent.pos.y + r * cos(n), 0);
    n = (k + 1) * (M_PI / 8);
    glVertex3f(agent.pos.x + r * sin(n), agent.pos.y + r * cos(n), 0);
  }
  // and spike
  glColor3f(0.5, 0, 0);
  glVertex3f(agent.pos.x, agent.pos.y, 0);
  glVertex3f(agent.pos.x + (3 * r * agent.spikeLength) * cos(agent.angle),
             agent.pos.y + (3 * r * agent.spikeLength) * sin(agent.angle), 0);
  glEnd();

  // and health
  int xo = 18;
  int yo = -15;
  glBegin(GL_TRIANGLES);
  // black background
  glColor3f(0, 0, 0);
  drawRectangle(agent.pos.x + xo, agent.pos.y + yo, 5, 40);

  // health
  glColor3f(0, 0.8, 0);
  drawRectangle(agent.pos.x + xo, agent.pos.y + yo + 20 * (2 - agent.health), 5,
                40 - 20 * (2 - agent.health));

  // if this is a hybrid, we want to put a marker down
  if (agent.hybrid) {
    glColor3f(0, 0, 0.8);
    drawRectangle(agent.pos.x + xo + 6, agent.pos.y + yo, 6, 10);
  }

  glColor3f(1 - agent.herbivore, agent.herbivore, 0);
  drawRectangle(agent.pos.x + xo + 6, agent.pos.y + yo + 12, 6, 10);

  // how much sound is this bot making?
  glColor3f(agent.soundmul, agent.soundmul, agent.soundmul);
  drawRectangle(agent.pos.x + xo + 6, agent.pos.y + yo + 24, 6, 10);

  // draw giving/receiving
  if (agent.dfood != 0) {

    float mag = cap(abs(agent.dfood) / conf::FOODTRANSFER / 3);
    if (agent.dfood > 0)
      glColor3f(0, mag, 0); // draw boost as green outline
    else
      glColor3f(mag, 0, 0);
    drawRectangle(agent.pos.x + xo + 6, agent.pos.y + yo + 36, 6, 10);
  }

  glEnd();

  // print stats if zoomed in enough
  if (scalemult > .7) {
    // generation count
    sprintf(buf2, "%i", agent.gencount);
    RenderString(agent.pos.x - conf::BOTRADIUS * 1.5,
                 agent.pos.y + conf::BOTRADIUS * 1.8,
                 GLUT_BITMAP_TIMES_ROMAN_24, buf2, 0.0f, 0.0f, 0.0f);
    // age
    sprintf(buf2, "%i", agent.age);
    RenderString(agent.pos.x - conf::BOTRADIUS * 1.5,
                 agent.pos.y + conf::BOTRADIUS * 1.8 + 12,
                 GLUT_BITMAP_TIMES_ROMAN_24, buf2, 0.0f, 0.0f, 0.0f);

    // health
    sprintf(buf2, "%.2f", agent.health);
    RenderString(agent.pos.x - conf::BOTRADIUS * 1.5,
                 agent.pos.y + conf::BOTRADIUS * 1.8 + 24,
                 GLUT_BITMAP_TIMES_ROMAN_24, buf2, 0.0f, 0.0f, 0.0f);

    // repcounter
    sprintf(buf2, "%.2f", agent.repcounter);
    RenderString(agent.pos.x - conf::BOTRADIUS * 1.5,
                 agent.pos.y + conf::BOTRADIUS * 1.8 + 36,
                 GLUT_BITMAP_TIMES_ROMAN_24, buf2, 0.0f, 0.0f, 0.0f);
  }
}

void GLView::drawFood(int x, int y, float quantity) {
  // draw food
  if (drawfood) {
    glBegin(GL_TRIANGLES);
    glColor3f(0.9 - quantity, 0.9 - quantity, 1.0 - quantity);
    drawRectangle(x * conf::CZ, y * conf::CZ, conf::CZ, conf::CZ);
    glEnd();
  }
}

void GLView::setBase(Base *b) { base = b; }
