#ifndef FOOD_H
#define FOOD_H
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

#define FOOD_SQUARES_WIDTH (WIDTH / CZ)
#define FOOD_SQUARES_HEIGHT (HEIGHT / CZ)
#define TOTAL_FOOD_SQUARES (FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT)

struct FoodGridItem {
  float amt;
  uint32_t food_sorted_index;
};

struct FoodGrid {
  uint32_t food_pivot;
  uint32_t food_sorted[TOTAL_FOOD_SQUARES];
  struct FoodGridItem food[FOOD_SQUARES_WIDTH][FOOD_SQUARES_HEIGHT];
};

void foodGrid_init(struct FoodGrid *foodGrid);
float foodGrid_getFoodAmount(struct FoodGrid *foodGrid, int32_t x, int32_t y);
float foodGrid_growFood(struct FoodGrid *foodGrid, int32_t x, int32_t y, float amt);
float foodGrid_takeFood(struct FoodGrid *foodGrid, int32_t x, int32_t y, float amt);


#endif
