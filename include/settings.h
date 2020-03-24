// see README.markdown -> BRAIN MAPPING for explanation of input and output size
#ifndef SETTINGS_H
#define SETTINGS_H

// Includes
#include <math.h> // for the math consts below

// Global variables
extern int VERBOSE;
extern int HEADLESS;
extern int NUM_THREADS;
extern int MAX_EPOCHS;
extern int MAX_SECONDS;

#define INPUTSIZE 42
#define OUTPUTSIZE 18
#define BRAINSIZE 250
#define CONNS 5

// WORLD / WINDOW SETTINGS -------------------------------------
// const int WIDTH = 2000;  //width and height of simulation world
// const int HEIGHT = 1500;
#define WIDTH 4000
#define HEIGHT 2500
 // computer window width and height
#define WWIDTH 1400
#define WHEIGHT 1000

#define CZ 50
// cell size in pixels, for food squares. Should divide well into Width Height

#define CLOSED 0
// world is closed and no new agents are added

// REPORTING --------------------------------------------------
#define REPORTS_PER_EPOCH 50
// number of times to record data and output status info, per epoch

// BOT PROPERTIES ---------------------------------------------
#define NUMBOTS 1000
// initially
#define NUMBOTS_MIN 20
// for open world, the threshold to start adding bots
#define BOTRADIUS 10
// for drawing
#define BOTSPEED 0.1
// how fast they can move
#define SPIKESPEED 0.01
// how quickly can attack spike go up?
#define SPIKEMULT 2
// essentially the strength of every spike impact
#define BOOSTSIZEMULT 2
// how much boost do agents get? when boost neuron is on
#define DIST 150
// how far can the eyes see, ears hear, and nose smell on each bot?
#define DIST_GROUPING 40
// how close must another agent be to get grouping health gain
#define EYE_SENSITIVITY 2
// how sensitive are the eyes?
#define BLOOD_SENSITIVITY 2
// how sensitive are blood sensors?
#define METAMUTRATE1 0.002
// what is the change in MUTRATE1 and 2 on reproduction
#define METAMUTRATE2 0.05
#define OLD_AGE_THRESHOLD = 500
// at what age do they start lossing health for being old?

// REPRODUCTION ----------------------------------------------
#define BABIES 2
// number of babies per agent when they reproduce
#define REPRATEH 7
// reproduction rate for herbivors
#define REPRATEC 7
// reproduction rate for carnivors
#define REP_MIN_HEALTH .75
// health level required of agent before it can reproduce

// HEALTH DEDUCTIONS
#define LOSS_BASE 0.00010
// loss of health for simply being alive (like breathing)
#define LOSS_SHOUTING 0.00005
// loss of health from shouting
#define LOSS_SPEED 0.00005
// loss of health for movement speed
#define LOSS_TEMP 0.00005
// loss of health from temperature distribution across world
#define LOSS_AGE 0.00005
// loss of health from old age
#define GAIN_GROUPING 0.00003
// addition of health for each bot near it, as a ratio of closeness
             // (thermal sharing)

// FOOD SETTINGS -----------------------------------------------
#define FOOD_MODEL_RAND 1
#define FOOD_MODEL_GROW 2

#define FOOD_MODEL FOOD_MODEL_RAND
// what kind of food appearance is to be used
#define FOODGROWTH 0.0005
// how quickly does food grow on a square. only
                                 // used with FOOD_MODEL_GROW
#define FOODINTAKE 0.00225
// how much does every agent consume?
#define FOODWASTE 0.001
// how much food disapears if agent eats?
#define FOODMAX 0.5
// how much food per cell can there be at max?
#define FOODADDFREQ 8
// how often does random square get to full food?
                           // (the lower the more often food is added)
#define FOOD_DEAD .1
// what percent of FOOD MAX does a dead agent create that is not eaten by carnivores?
#define FOOD_MEAT_VALUE 0.75
// percentage that health is transferred to another agent when eaten

// how much is transfered between two agents trading food? per iteration
#define FOODTRANSFER 0.001

// how far away is food shared between bots?
#define FOOD_SHARING_DISTANCE 40

// when bot is killed, how far is its body distributed?
#define FOOD_DISTRIBUTION_RADIUS 100

// GEOMETRIC CALCULATION CONSTATNS (DO NOT CHANGE)
#define PI8 (M_PI / 8 / 2) // pi/8/2
#define PI38 (3 * PI8)     // 3pi/8/2
#define reportInterval (10000 / REPORTS_PER_EPOCH)

#endif
