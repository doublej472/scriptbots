// sim_config.h — world size, bot properties, food, health, reproduction tuning
#ifndef SIM_CONFIG_H
#define SIM_CONFIG_H
#include <math.h>
#include <stdint.h>

// ---- WORLD / WINDOW ----
#define CZ 64
// cell size in pixels, for food squares. Should divide well into Width Height

#define WIDTH (CZ * 3000)
#define HEIGHT (CZ * 2000)

// computer window width and height
#define WWIDTH 1280
#define WHEIGHT 720

#define CLOSED 0
// world is closed and no new agents are added

// ---- REPORTING ----
#define REPORTS_PER_EPOCH 100
// number of times to record data and output status info, per epoch
#define reportInterval (10000 / REPORTS_PER_EPOCH)

// ---- BOT PROPERTIES ----
#define NUMBOTS 10000           // initially
#define NUMBOTS_MIN 20          // for open world, the threshold to start adding bots
#define NUMBOTS_CLOSE 256       // maximum number of bots to consider when checking close bots
#define BOTRADIUS 10.0f         // for drawing
#define BOTSPEED 0.1f           // how fast they can move
#define SPIKESPEED 0.02f        // how quickly can attack spike go up?
#define SPIKEMULT 2.0f          // essentially the strength of every spike impact
#define BOOSTSIZEMULT 2.0f      // how much boost do agents get? when boost neuron is on
#define DIST 225.0f             // how far can the eyes see, ears hear, and nose smell on each bot?
#define DIST_GROUPING 40.0f     // how close must another agent be to get grouping health gain
#define EYE_SENSITIVITY 2.0f    // how sensitive are the eyes?
#define BLOOD_SENSITIVITY 2.0f  // how sensitive are blood sensors?
#define METAMUTRATE1 0.01f      // what is the change in MUTRATE1 on reproduction
#define METAMUTRATE2 0.15f      // what is the change in MUTRATE2 on reproduction
#define OLD_AGE_THRESHOLD 50000 // at what age do they start losing health for being old?

// ---- REPRODUCTION ----
#define BABIES 3             // number of babies per agent when they reproduce
#define REPRATEH 6           // reproduction rate for herbivores
#define REPRATEC 6           // reproduction rate for carnivores
#define REP_MIN_HEALTH 0.80f // health level required of agent before it can reproduce

// ---- HEALTH DEDUCTIONS ----
#define LOSS_BASE 0.00010f        // loss of health for simply being alive
#define LOSS_SHOUTING 0.00005f    // loss of health from shouting
#define LOSS_SPEED 0.00005f       // loss of health for movement speed
#define LOSS_BOOST 0.00030f       // loss of health for boosting
#define LOSS_AGE 0.00015f         // loss of health from old age
#define GAIN_GROUPING 0.000025f   // health per nearby-agent · ratio-unit (offset by crowding)
#define CROWDING_LIMIT 8          // agents beyond this trigger quadratic crowding penalty
#define CROWDING_PENALTY 0.00004f // quadratic penalty per extra agent (× excess²)

// ---- FOOD ----
#define FOODGROWTH 0.35f                    // how quickly does food grow on a square
#define FOODINTAKE 0.00225f                 // how much does every agent consume?
#define FOODMAX 0.6f                        // how much food per cell can there be at max?
#define FOOD_ADD_PER_FRAME 2.0f             // total food budget distributed per frame
#define FOOD_SPARSE_THRESHOLD 0.005f        // fraction of grid alive below which we seed randomly
#define FOODTRANSFER 0.001f                 // how much is transferred between two agents trading food?
#define FOOD_SHARING_DISTANCE 40.0f         // how far away is food shared between bots?
#define FOOD_INIT_ITER 50000                // Number of initial food iterations
#define FOOD_DISTRIBUTION_RADIUS DIST       // when bot is killed, how far is its body distributed?
#define FOOD_DISTRIBUTION_MAX NUMBOTS_CLOSE

// ---- GEOMETRIC CONSTANTS (do not change) ----
#define PI8 ((float)(M_PI / 8.0f / 2.0f)) // pi/8/2 = pi/16
#define PI38 (3.0f * PI8)                 // 3pi/8/2 = 3pi/16
// Cone pre-test constants (derived from PI8/PI38)
#define COS_PI16 0.98078528f  // cos(pi/16)
#define SIN_PI16 0.19509032f  // sin(pi/16)
#define TAN_3PI16 0.66817864f // tan(3pi/16)

#endif
