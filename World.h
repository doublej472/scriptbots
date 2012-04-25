#ifndef WORLD_H
#define WORLD_H

#include "View.h"
#include "Agent.h"
#include "settings.h"
#include <vector>
#include "boost.h"
#include "PerfTimer.h"
#include <sys/time.h>
#include <sys/resource.h>

class World
{
	// Serialization ------------------------------------------
	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned int version )
	{
		// Add all class variables here:
		ar & modcounter;
		ar & current_epoch;
		ar & idcounter;
		ar & numAgentsAdded;
		ar & FW;
		ar & FH;
		ar & fx;
		ar & fy;
		ar & food;
		ar & CLOSED;
		ar & touch;
		ar & agents;
	}
	// ---------------------------------------------------------
 public:
	World();
    ~World(){};
    
    void update();
    void reset();
    
    void draw(View* view, bool drawfood);
    
    bool isClosed() const;
    void setClosed(bool close);
    
    /**
     * Returns the number of herbivores and 
     * carnivores in the world.
     * first : num herbs
     * second : num carns
     */
    std::pair<int,int> numHerbCarnivores() const;

	void printState();
    int numAgents() const;
    int epoch() const;

    //mouse interaction
    void processMouse(int button, int state, int x, int y);

    void addNewByCrossover();
    void addRandomBots(int num);
    void addCarnivore();

 private:

    void setInputsRunBrain();
    void processOutputs();
    
    void growFood(int x, int y);

    void writeReport();
    
    void reproduce(int ai, float MR, float MR2);
    
    int modcounter; // temp not private	
    int current_epoch;
    int idcounter;
	int numAgentsAdded; // counts how many agents have been artifically added per reporting iteration
	
    std::vector<Agent> agents;
    
    // food
    int FW;
    int FH;
    int fx;
    int fy;
    float food[conf::WIDTH/conf::CZ][conf::HEIGHT/conf::CZ];
    bool CLOSED; //if environment is closed, then no random bots are added per time interval

    bool touch;

	double startTime; // used for tracking fps

};

#endif // WORLD_H
