// see README.markdown -> BRAIN MAPPING for explanation of input and output size
#ifndef SETTINGS_H
#define SETTINGS_H
#include <math.h> // for the math consts below
#include <stdint.h>

// Global variables
extern int32_t VERBOSE;
extern int32_t HEADLESS;
extern int32_t NUM_THREADS;
extern int32_t MAX_EPOCHS;
extern int32_t MAX_SECONDS;

// How many hidden layers this brain has
#define BRAIN_DEPTH 4

// MUST BE MULTIPLE OF 8 real inputs and outputs
// Essentially how many neurons are in a given layer
#define BRAIN_WIDTH (8 * 4)

// How many inputs / outputs are dedicated to planning
// which means this number of outputs will be copied to inputs
// on the next brain tick
#define PLANSIZE (BRAIN_WIDTH - 19)

// 19 and 18 here represent "real" inputs and outputs
#define INPUTSIZE (19 + PLANSIZE)
#define OUTPUTSIZE (18 + PLANSIZE)

// WORLD / WINDOW SETTINGS -------------------------------------
#define CZ 50
// cell size in pixels, for food squares. Should divide well into Width Height

// const int32_t WIDTH = 2000;  //width and height of simulation world
// const int32_t HEIGHT = 1500;
#define WIDTH (CZ * 300)
#define HEIGHT (CZ * 200)
// computer window width and height
#define WWIDTH 1400
#define WHEIGHT 1000

#define CLOSED 0
// world is closed and no new agents are added

// REPORTING --------------------------------------------------
#define REPORTS_PER_EPOCH 50
// number of times to record data and output status info, per epoch

// BOT PROPERTIES ---------------------------------------------
#define NUMBOTS 2000
// initially
#define NUMBOTS_MIN 20
// for open world, the threshold to start adding bots
#define NUMBOTS_CLOSE 32
// maximum number of bots to consider when checking close bots
#define BOTRADIUS 10.0f
// for drawing
#define BOTSPEED 0.1f
// how fast they can move
#define SPIKESPEED 0.02f
// how quickly can attack spike go up?
#define SPIKEMULT 2.0f
// essentially the strength of every spike impact
#define BOOSTSIZEMULT 2.0f
// how much boost do agents get? when boost neuron is on
#define DIST 200.0f
// how far can the eyes see, ears hear, and nose smell on each bot?
#define DIST_GROUPING 40.0f
// how close must another agent be to get grouping health gain
#define EYE_SENSITIVITY 2.0f
// how sensitive are the eyes?
#define BLOOD_SENSITIVITY 2.0f
// how sensitive are blood sensors?
#define METAMUTRATE1 0.01f
// what is the change in MUTRATE1 and 2 on reproduction
#define METAMUTRATE2 0.15f
#define OLD_AGE_THRESHOLD = 50000
// at what age do they start lossing health for being old?

// REPRODUCTION ----------------------------------------------
#define BABIES 3
// number of babies per agent when they reproduce
#define REPRATEH 7
// reproduction rate for herbivors
#define REPRATEC 6
// reproduction rate for carnivors
#define REP_MIN_HEALTH 0.75f
// health level required of agent before it can reproduce

// HEALTH DEDUCTIONS
#define LOSS_BASE 0.00010f
// loss of health for simply being alive (like breathing)
#define LOSS_SHOUTING 0.00005f
// loss of health from shouting
#define LOSS_SPEED 0.00005f
// loss of health for movement speed
#define LOSS_BOOST 0.00030f
// loss of health for boosting
#define LOSS_TEMP 0.00005f
// loss of health from temperature distribution across world
#define LOSS_AGE 0.00015f
// loss of health from old age
#define GAIN_GROUPING 0.00003f
// addition of health for each bot near it, as a ratio of closeness
// (thermal sharing)

// FOOD SETTINGS -----------------------------------------------
// How many food squares to check every FOODADDFREQ?
#define FOODSQUARES 800
// what kind of food appearance is to be used
#define FOODGROWTH 0.0005f
// how quickly does food grow on a square. only
// used with FOOD_MODEL_GROW
#define FOODINTAKE 0.00225f
// how much does every agent consume?
#define FOODWASTE 0.001f
// how much food disapears if agent eats?
#define FOODMAX 0.6f
// how much food per cell can there be at max?
#define FOODADDFREQ 15
// how often does random square get to full food?
// (the lower the more often food is added)
#define FOOD_DEAD 0.1f
// what percent of FOOD MAX does a dead agent create that is not eaten by
// carnivores?
#define FOOD_MEAT_VALUE 0.75f
// percentage that health is transferred to another agent when eaten

// how much is transfered between two agents trading food? per iteration
#define FOODTRANSFER 0.001f

// how far away is food shared between bots?
#define FOOD_SHARING_DISTANCE 40.0f

// when bot is killed, how far is its body distributed?
#define FOOD_DISTRIBUTION_RADIUS DIST

// max number of agents to distribute to
#define FOOD_DISTRIBUTION_MAX NUMBOTS_CLOSE

// GEOMETRIC CALCULATION CONSTATNS (DO NOT CHANGE)
#define PI8 ((float)(M_PI / 8.0f / 2.0f)) // pi/8/2
#define PI38 (3.0f * PI8)                 // 3pi/8/2
#define reportInterval (10000 / REPORTS_PER_EPOCH)

#endif
