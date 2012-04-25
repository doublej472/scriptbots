// see README.markdown -> BRAIN MAPPING for explanation of input and output size
#define INPUTSIZE 42
#define OUTPUTSIZE 18
#define BRAINSIZE 200
#define CONNS 4

#ifndef SETTINGS_H
#define SETTINGS_H

// Includes
#include "PerfTimer.h"
#include <cmath> // for the math consts below

// Global variables
extern bool VERBOSE;
extern bool HEADLESS;
extern int NUM_THREADS;
extern PerfTimer TIMER; // used throughout program to do benchmark timing

// Program configuration settings
namespace conf {

	// WORLD / WINDOW SETTINGS -------------------------------------
    const int WIDTH = 2000;  //width and height of simulation world
    const int HEIGHT = 1500;
    //const int WIDTH = 4000;  //width and height of simulation world
	//    const int HEIGHT = 2500;	
    const int WWIDTH = 1400;  //computer window width and height
    const int WHEIGHT = 1000;
    
    const int CZ = 50; //cell size in pixels, for food squares. Should divide well into Width Height

	const bool CLOSED = false; // world is closed and no new agents are added

	// REPORTING --------------------------------------------------
	const int   REPORTS_PER_EPOCH = 5; // number of times to record data and output status info, per epoch
	
	// BOT PROPERTIES ---------------------------------------------
    const int   NUMBOTS=200;         //initially
	const int   NUMBOTS_MIN=200;     //for open world, the threshold to start adding bots
    const float BOTRADIUS=10;        //for drawing
    const float BOTSPEED= 0.1;       //how fast they can move
    const float SPIKESPEED= 0.01;    //how quickly can attack spike go up?
    const float SPIKEMULT= 2;        //essentially the strength of every spike impact
    const float BOOSTSIZEMULT=2;     //how much boost do agents get? when boost neuron is on
    const float DIST= 150;	     	 //how far can the eyes see, ears hear, and nose smell on each bot?
	const float DIST_GROUPING = 40;  //how close must another agent be to get grouping health gain
    const float EYE_SENSITIVITY= 2;  //how sensitive are the eyes?
    const float BLOOD_SENSITIVITY= 2; //how sensitive are blood sensors?
    const float METAMUTRATE1= 0.002; //what is the change in MUTRATE1 and 2 on reproduction
    const float METAMUTRATE2= 0.05;
	const int   OLD_AGE_THRESHOLD = 500; // at what age do they start lossing health for being old?

	// REPRODUCTION ----------------------------------------------
    const int   BABIES=2; //number of babies per agent when they reproduce
    const float REPRATEH=7; //reproduction rate for herbivors
    const float REPRATEC=7; //reproduction rate for carnivors
	const float REP_MIN_HEALTH=.75; // health level required of agent before it can reproduce

	// HEALTH DEDUCTIONS
	//const float LOSS_BASE     = 0.0001; // loss of health for simply being alive (like breathing)
	const float LOSS_BASE     = 0.00006; // loss of health for simply being alive (like breathing)	
	const float LOSS_SHOUTING = 0.00005; // loss of health from shouting
	const float LOSS_SPEED    = 0.00005; // loss of health for movement speed
	const float LOSS_TEMP     = 0.00005; // loss of health from temperature distribution across world
	const float LOSS_AGE      = 0.00005; // loss of health from old age
	const float GAIN_GROUPING = 0.00007; // addition of health for each bot near it, as a ratio of closeness (thermal sharing)
		
	// FOOD SETTINGS -----------------------------------------------
	const int FOOD_MODEL_RAND = 1; // Food Model Options
	const int FOOD_MODEL_GROW = 2; // Food Model Options

	const int   FOOD_MODEL = FOOD_MODEL_RAND; //what kind of food appearance is to be used
    const float FOODGROWTH= 0.0005; //how quickly does food grow on a square. only used with FOOD_MODEL_GROW
    const float FOODINTAKE= 0.00225; //how much does every agent consume?
    const float FOODWASTE= 0.001; //how much food disapears if agent eats?
    const float FOODMAX= 0.5; //how much food per cell can there be at max?
    const int   FOODADDFREQ= 10; //how often does random square get to full food? (the lower the more often food is added)
	const float FOOD_DEAD = .1; // what percent of FOOD MAX does a dead agent create that is not eaten by carnivores?
	const float FOOD_MEAT_VALUE = 2.0; // percentage that health is transferred to another agent when eaten
		
    const float FOODTRANSFER= 0.001; //how much is transfered between two agents trading food? per iteration
    const float FOOD_SHARING_DISTANCE= 40; //how far away is food shared between bots?

    const float FOOD_DISTRIBUTION_RADIUS=100; //when bot is killed, how far is its body distributed?

	// GEOMETRIC CALCULATION CONSTATNS (DO NOT CHANGE)
	const float PI8  = M_PI/8/2; //pi/8/2
    const float PI38 = 3*PI8; //3pi/8/2
	const int reportInterval = 10000 / REPORTS_PER_EPOCH;					

}

#endif
