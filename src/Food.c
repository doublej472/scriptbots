#include "Food.h"

void foodGrid_init(struct FoodGrid *foodGrid) {

  foodGrid->food_pivot = 0;

  // Initialize food grid and sorted indices
  for (int32_t x = 0; x < FOOD_SQUARES_WIDTH; x++) {
    for (int32_t y = 0; y < FOOD_SQUARES_HEIGHT; y++) {
      uint32_t index = x + y * FOOD_SQUARES_WIDTH;
      foodGrid->food[y][x] = (struct FoodGridItem){0.0f, index};
      foodGrid->food_sorted[index] = index;
    }
  }
}

float foodGrid_getFoodAmount(struct FoodGrid *foodGrid, int32_t x, int32_t y) {
  // check if food square is inside the world
  if (x >= 0 && x < FOOD_SQUARES_WIDTH && y >= 0 && y < FOOD_SQUARES_HEIGHT) {
    return foodGrid->food[y][x].amt;
  }
  return 0.0f;
}

// Ensure food square is correctly placed in the sorted list
static void foodGrid_place(struct FoodGrid *foodGrid, int32_t x, int32_t y) {
  struct FoodGridItem *item = &foodGrid->food[y][x];

  // If food is alive ensure it's before the pivot, otherwise after
  if (item->amt >= 0.0001f) {
    if (foodGrid->food_pivot < TOTAL_FOOD_SQUARES - 1 && item->food_sorted_index >= foodGrid->food_pivot) {
      // Lookup old food item we are replacing
      uint32_t new_food_sorted_idx = foodGrid->food_pivot;
      uint32_t old_food_idx = foodGrid->food_sorted[new_food_sorted_idx];
      uint32_t tx = old_food_idx % FOOD_SQUARES_WIDTH;
      uint32_t ty = old_food_idx / FOOD_SQUARES_WIDTH;
      struct FoodGridItem *old_item = &foodGrid->food[ty][tx];

      // Swap food_sorted members
      uint32_t tmp = foodGrid->food_sorted[new_food_sorted_idx];
      foodGrid->food_sorted[new_food_sorted_idx] = foodGrid->food_sorted[item->food_sorted_index];
      foodGrid->food_sorted[item->food_sorted_index] = tmp;

      // Swap food item reverse index
      old_item->food_sorted_index = item->food_sorted_index;
      item->food_sorted_index = new_food_sorted_idx;

      // Update the pivot point
      foodGrid->food_pivot++;
    }
  } else {
    if (foodGrid->food_pivot > 0 && item->food_sorted_index < foodGrid->food_pivot) {
      // Lookup old food item we are replacing
      uint32_t new_food_sorted_idx = foodGrid->food_pivot - 1;
      uint32_t old_food_idx = foodGrid->food_sorted[new_food_sorted_idx];
      uint32_t tx = old_food_idx % FOOD_SQUARES_WIDTH;
      uint32_t ty = old_food_idx / FOOD_SQUARES_WIDTH;
      struct FoodGridItem *old_item = &foodGrid->food[ty][tx];

      // Swap food_sorted members
      uint32_t tmp = foodGrid->food_sorted[new_food_sorted_idx];
      foodGrid->food_sorted[new_food_sorted_idx] = foodGrid->food_sorted[item->food_sorted_index];
      foodGrid->food_sorted[item->food_sorted_index] = tmp;

      // Swap food item reverse index
      old_item->food_sorted_index = item->food_sorted_index;
      item->food_sorted_index = new_food_sorted_idx;

      // Update the pivot point
      foodGrid->food_pivot--;
    }
  }
}

// Grow food around square
// Returns amount that was grown
float foodGrid_growFood(struct FoodGrid *foodGrid, int32_t x, int32_t y, float amt) {
  // check if food square is inside the world
  if (x >= 0 && x < FOOD_SQUARES_WIDTH && y >= 0 && y < FOOD_SQUARES_HEIGHT && foodGrid->food[y][x].amt < FOODMAX) {
    foodGrid->food[y][x].amt += amt;
    if (foodGrid->food[y][x].amt > FOODMAX) {
      float sub = foodGrid->food[y][x].amt - FOODMAX;
      foodGrid->food[y][x].amt -= sub;
      amt -= sub;
    }

    foodGrid_place(foodGrid, x, y);

    return amt;
  }
  return 0.0f;
}

// Take food from square
// Returns amount that was taken
float foodGrid_takeFood(struct FoodGrid *foodGrid, int32_t x, int32_t y, float amt) {
  // check if food square is inside the world
  if (x >= 0 && x < FOOD_SQUARES_WIDTH && y >= 0 && y < FOOD_SQUARES_HEIGHT && foodGrid->food[y][x].amt > 0.0f) {
    foodGrid->food[y][x].amt -= amt;
    if (foodGrid->food[y][x].amt < 0.0f) {
      float sub = -foodGrid->food[y][x].amt;
      foodGrid->food[y][x].amt += sub;
      amt -= sub;
    }

    foodGrid_place(foodGrid, x, y);

    return amt;
  }
  return 0.0f;
}

float foodGrid_getTotalFood(struct FoodGrid *foodGrid) {
  float total_food = 0.0f;
  for (size_t i = 0; i < foodGrid->food_pivot; i++) {
    uint32_t x = foodGrid->food_sorted[i] % FOOD_SQUARES_WIDTH;
    uint32_t y = foodGrid->food_sorted[i] / FOOD_SQUARES_WIDTH;
    total_food += foodGrid->food[y][x].amt;
  }
  return total_food;
}
