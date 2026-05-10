#ifndef FOOD_H
#define FOOD_H
#include "settings.h"
#include <stddef.h>
#include <stdint.h>

#define FOOD_SQUARES_WIDTH (WIDTH / CZ)
#define FOOD_SQUARES_HEIGHT (HEIGHT / CZ)
#define TOTAL_FOOD_SQUARES (FOOD_SQUARES_WIDTH * FOOD_SQUARES_HEIGHT)

struct FoodGrid {
  uint32_t grid_w, grid_h;    // cell dimensions
  uint32_t total_cells;
  uint32_t food_pivot;        // number of active (non-empty) cells
  uint32_t *food_sorted;      // [total_cells] — active sorted cell indices
  float    *food_amounts;     // [total_cells] — food amount per cell (SSBO-compatible)
  uint32_t *food_indices;     // [total_cells] — sorted cell index per cell
};

void foodGrid_init(struct FoodGrid *foodGrid, uint32_t w, uint32_t h);
void foodGrid_free(struct FoodGrid *foodGrid);
float foodGrid_getFoodAmount(struct FoodGrid *foodGrid, int32_t x, int32_t y);
float foodGrid_growFood(struct FoodGrid *foodGrid, int32_t x, int32_t y, float amt);
float foodGrid_takeFood(struct FoodGrid *foodGrid, int32_t x, int32_t y, float amt);
float foodGrid_getTotalFood(struct FoodGrid *foodGrid);

#endif
