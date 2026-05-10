#include "Food.h"
#include <stdio.h>
#include <stdlib.h>

void foodGrid_init(struct FoodGrid *g, uint32_t w, uint32_t h) {
  g->grid_w      = w;
  g->grid_h      = h;
  g->total_cells = w * h;
  g->food_pivot  = 0;

  g->food_amounts = calloc(g->total_cells, sizeof(float));
  g->food_indices = calloc(g->total_cells, sizeof(uint32_t));
  g->food_sorted  = malloc(g->total_cells * sizeof(uint32_t));

  for (uint32_t i = 0; i < g->total_cells; i++) {
    g->food_sorted[i] = i;
    g->food_indices[i] = i;
  }
}

void foodGrid_free(struct FoodGrid *g) {
  free(g->food_amounts);
  free(g->food_indices);
  free(g->food_sorted);
  g->food_amounts = NULL;
  g->food_indices = NULL;
  g->food_sorted  = NULL;
}

float foodGrid_getFoodAmount(struct FoodGrid *g, int32_t x, int32_t y) {
  if (x >= 0 && (uint32_t)x < g->grid_w && y >= 0 && (uint32_t)y < g->grid_h)
    return g->food_amounts[y * g->grid_w + x];
  return 0.0f;
}

// Swap food_sorted entries at positions a and b, and update their reverse indices
static void foodGrid_swap(struct FoodGrid *g, uint32_t a, uint32_t b) {
  uint32_t sa = g->food_sorted[a];
  uint32_t sb = g->food_sorted[b];
  g->food_sorted[a] = sb;
  g->food_sorted[b] = sa;
  g->food_indices[sa] = b;
  g->food_indices[sb] = a;
}

// Ensure cell is on the correct side of the pivot (active / inactive)
static void foodGrid_place(struct FoodGrid *g, uint32_t flat_idx) {
  uint32_t sorted_idx = g->food_indices[flat_idx];
  float amt = g->food_amounts[flat_idx];

  if (amt >= 0.0001f) {
    // Move into active region [0, pivot)
    if (sorted_idx >= g->food_pivot) {
      foodGrid_swap(g, sorted_idx, g->food_pivot);
      g->food_pivot++;
    }
  } else {
    // Move into inactive region [pivot, total)
    if (sorted_idx < g->food_pivot) {
      foodGrid_swap(g, sorted_idx, g->food_pivot - 1);
      g->food_pivot--;
    }
  }
}

float foodGrid_growFood(struct FoodGrid *g, int32_t x, int32_t y, float amt) {
  if (x < 0 || (uint32_t)x >= g->grid_w || y < 0 || (uint32_t)y >= g->grid_h)
    return 0.0f;

  uint32_t idx = y * g->grid_w + x;
  float *a = &g->food_amounts[idx];
  if (*a >= FOODMAX)
    return 0.0f;

  *a += amt;
  if (*a > FOODMAX) {
    float sub = *a - FOODMAX;
    *a -= sub;
    amt -= sub;
  }

  foodGrid_place(g, idx);
  return amt;
}

float foodGrid_takeFood(struct FoodGrid *g, int32_t x, int32_t y, float amt) {
  if (x < 0 || (uint32_t)x >= g->grid_w || y < 0 || (uint32_t)y >= g->grid_h)
    return 0.0f;

  uint32_t idx = y * g->grid_w + x;
  float *a = &g->food_amounts[idx];
  if (*a <= 0.0f)
    return 0.0f;

  *a -= amt;
  if (*a < 0.0f) {
    float sub = -(*a);
    *a += sub;
    amt -= sub;
  }

  foodGrid_place(g, idx);
  return amt;
}

float foodGrid_getTotalFood(struct FoodGrid *g) {
  float total = 0.0f;
  for (uint32_t i = 0; i < g->food_pivot; i++) {
    uint32_t idx = g->food_sorted[i];
    total += g->food_amounts[idx];
  }
  return total;
}
