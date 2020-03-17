#ifndef WORLD_H
#define WORLD_H

#include "Agent.h"
#include "View.h"
#include "settings.h"
#include "vec.h"
#include <sys/resource.h>
#include <sys/time.h>

class World {
public:
  World();
  ~World(){};

  void update();
  void reset();

  void draw(View *view, bool drawfood);

  bool isClosed() const;
  void setClosed(bool close);

  /**
   * Returns the number of herbivores and
   * carnivores in the world.
   * first : num herbs
   * second : num carns
   */
  std::pair<int, int> numHerbCarnivores() const;

  void printState();
  int numAgents() const;
  int epoch() const;

  // mouse interaction
  void processMouse(int button, int state, int x, int y);

  void addNewByCrossover();
  void addRandomBots(int num);
  void addCarnivore();

  bool stopSim;

private:
  void setInputsRunBrain();
  void processOutputs();

  void growFood(int x, int y);

  void writeReport();

  void reproduce(int ai, float MR, float MR2);

  int modcounter; // temp not private
  int current_epoch;
  int idcounter;
  int numAgentsAdded; // counts how many agents have been artifically added per
                      // reporting iteration

  AVec agents;

  // food
  int FW;
  int FH;
  int fx;
  int fy;
  float food[conf::WIDTH / conf::CZ][conf::HEIGHT / conf::CZ];
  bool CLOSED; // if environment is closed, then no random bots are added per
               // time interval

  bool touch;

  struct timespec startTime;      // used for tracking fps
  struct timespec totalStartTime; // used for deciding when to quit the simulation
};

#endif // WORLD_H
