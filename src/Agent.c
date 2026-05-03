#include "Agent.h"

#include "helpers.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---- Brain helpers (flat float array, GPU-compatible layout) ----

void brain_init_random(float *w) {
    for (int i = 0; i < BRAIN_WEIGHT_FLOATS; i++)
        w[i] = (randf(0.0f, 1.0f) - 0.5f) * BRAIN_WEIGHT_RANGE * 2.0f;
}

void brain_mutate(float *w, float rate, float mag) {
    for (int i = 0; i < BRAIN_WEIGHT_FLOATS; i++) {
        if (randf(0.0f, 1.0f) < rate) {
            w[i] += (randf(0.0f, 1.0f) - 0.5f) * mag * 2.0f;
            if (w[i] > BRAIN_WEIGHT_RANGE)  w[i] = BRAIN_WEIGHT_RANGE;
            if (w[i] < -BRAIN_WEIGHT_RANGE) w[i] = -BRAIN_WEIGHT_RANGE;
        }
    }
}

// ---- Agent lifecycle ----

void agent_init(struct Agent *agent) {
    vector2f_init(&agent->pos, randf(0, WIDTH), randf(0, HEIGHT));
    agent->angle = randf(-M_PI, M_PI);
    agent->health = 1.0f + randf(0, 0.1f);
    agent->pending_damage = 0.0f;
    agent->pending_spiked = 0;
    agent->touch = 0;
    agent->age = 0;
    agent->spikeLength = 0;
    agent->red = 0;
    agent->gre = 0;
    agent->blu = 0;
    agent->w1 = 0;
    agent->w2 = 0;
    agent->soundmul = 1;
    agent->give = 0;
    agent->clockf1 = randf(5, 100);
    agent->clockf2 = randf(5, 100);
    agent->boost = 0;
    agent->indicator = 0;
    agent->gencount = 0;
    agent->selectflag = 0;
    agent->ir = 0;
    agent->ig = 0;
    agent->ib = 0;
    agent->hybrid = 0;
    agent->herbivore = randf(0, 1);
    agent->rep = 0;
    agent->repcounter = agent->herbivore * randf(REPRATEH - 0.1f, REPRATEH + 0.1f) +
                        (1.0f - agent->herbivore) * randf(REPRATEC - 0.1f, REPRATEC + 0.1f);
    agent->numchildren = 0;
    agent->MUTRATE1 = METAMUTRATE1;
    agent->MUTRATE2 = METAMUTRATE2;
    agent->spiked = 0;
    agent->sort_alive = 0;

    for (int i = 0; i < BRAIN_INPUT_SIZE; i++)  agent->in[i] = 0.0f;
    for (int i = 0; i < BRAIN_OUTPUT_SIZE; i++) agent->out[i] = 0.0f;

    agent->brain = malloc(BRAIN_WEIGHT_FLOATS * sizeof(float));
    brain_init_random(agent->brain);
}

void agent_print(struct Agent *agent) {
    printf("Agent age=%i\n", agent->age);
}

void agent_initevent(struct Agent *agent, float size, float r, float g, float b) {
    if (size > agent->indicator) {
        agent->indicator = size;
        agent->ir = r;
        agent->ig = g;
        agent->ib = b;
    }
}

void agent_reproduce(struct Agent *child, struct Agent *parent) {
    struct Vector2f fb;
    vector2f_init(&fb, randf(BOTRADIUS, BOTRADIUS * 3.0f), 0.0f);
    vector2f_rotate(&fb, -child->angle);
    vector2f_add(&child->pos, &parent->pos, &fb);

    parent->numchildren++;
    child->gencount = parent->gencount + 1;
    child->repcounter = child->herbivore * randf(REPRATEH - 0.2f, REPRATEH + 0.2f) +
                        (1.0f - child->herbivore) * randf(REPRATEC - 0.2f, REPRATEC + 0.2f);

    child->MUTRATE1 = parent->MUTRATE1;
    child->MUTRATE2 = parent->MUTRATE2;
    if (randf(0, 1) < 0.2f) child->MUTRATE1 = cap(randn(parent->MUTRATE1, METAMUTRATE1));
    if (randf(0, 1) < 0.2f) child->MUTRATE2 = randn(parent->MUTRATE2, METAMUTRATE2);
    if (child->MUTRATE1 < 0.02f) child->MUTRATE1 = 0.02f;
    if (child->MUTRATE2 < 0.02f) child->MUTRATE2 = 0.02f;
    child->herbivore = cap(randn(parent->herbivore, 0.03f));
    if (randf(0, 1) < child->MUTRATE1 * 5.0f)
        child->clockf1 = randn(child->clockf1, child->MUTRATE2);
    if (child->clockf1 < 2.0f) child->clockf1 = 2.0f;
    if (randf(0, 1) < child->MUTRATE1 * 5.0f)
        child->clockf2 = randn(child->clockf2, child->MUTRATE2);
    if (child->clockf2 < 2.0f) child->clockf2 = 2.0f;

    // Mutate brain (flat float array)
    memcpy(child->brain, parent->brain, BRAIN_WEIGHT_FLOATS * sizeof(float));
    brain_mutate(child->brain, child->MUTRATE1, child->MUTRATE2);
}

void agent_process_health(struct Agent *agent) {
    float healthloss = LOSS_BASE;
    if (agent->age > 500.0f)
        healthloss += (LOSS_AGE * ((agent->age - 500.0f) / 250.0f));

    if (agent->boost) {
        healthloss += LOSS_SPEED * BOTSPEED * ((fabsf(agent->w1) + fabsf(agent->w2)) / 2.0f) + LOSS_BOOST * agent->boost;
    } else {
        healthloss += LOSS_SPEED * BOTSPEED * (fabsf(agent->w1) + fabsf(agent->w2));
    }
    healthloss += LOSS_SHOUTING * agent->soundmul;
    agent->health -= healthloss;
}
