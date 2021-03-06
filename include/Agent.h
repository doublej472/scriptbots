#ifndef AGENT_H
#define AGENT_H

#include "MLPBrain.h"
#include "vmath.h"

#include "boost.h"
#include <string>

class Agent {
  // Serialization ------------------------------------------
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    // Add all class variables here:
    ar &pos;
    ar &health;
    ar &angle;
    ar &touch;
    ar &red;
    ar &gre;
    ar &blu;
    ar &w1;
    ar &w2;
    ar &boost;
    ar &spikeLength;
    ar &age;
    ar &spiked;
    ar &in;
    ar &out;
    ar &repcounter;
    ar &gencount;
    ar &hybrid;
    ar &clockf1;
    ar &clockf2;
    ar &soundmul;
    ar &indicator;
    ar &ir;
    ar &ig;
    ar &ib;
    ar &selectflag;
    ar &dfood;
    ar &give;
    ar &id;
    ar &herbivore;
    ar &MUTRATE1;
    ar &MUTRATE2;
    ar &temperature_preference;
    ar &brain;
  }
  // ---------------------------------------------------------
public:
  Agent();

  void printSelf();
  // for drawing purposes
  void initEvent(float size, float r, float g, float b);

  void tick();
  Agent reproduce(float MR, float MR2);
  Agent crossover(const Agent &other);

  Vector2f pos;

  float health; // in [0,2]. I cant remember why.
  float angle;  // of the bot

  bool touch; // is bot close to wall?

  float red;
  float gre;
  float blu;

  float w1; // wheel speeds
  float w2;
  bool boost; // is this agent boosting

  float spikeLength;
  int age;

  bool spiked;

  float in[INPUTSIZE];   // input: 2 eyes, sensors for R,G,B,proximity each,
                         // then Sound, Smell, Health
  float out[OUTPUTSIZE]; // output: Left, Right, R, G, B, SPIKE

  float repcounter;       // when repcounter gets to 0, this bot reproduces
  int gencount;           // generation counter
  bool hybrid;            // is this agent result of crossover?
  float clockf1, clockf2; // the frequencies of the two clocks of this bot
  float soundmul; // sound multiplier of this bot. It can scream, or be very
                  // sneaky. This is actually always set to output 8

  // variables for drawing purposes
  float indicator;
  float ir, ig, ib; // indicator colors
  int selectflag;   // is this agent selected?
  float dfood;      // what is change in health of this agent due to
                    // giving/receiving?

  float give; // is this agent attempting to give food to other agent?

  int id;

  // inhereted stuff
  float herbivore; // is this agent a herbivore? between 0 and 1
  float MUTRATE1;  // how often do mutations occur?
  float MUTRATE2;  // how significant are they?
  float
      temperature_preference; // what temperature does this agent like? [0 to 1]

  MLPBrain brain;
};

#endif // AGENT_H
