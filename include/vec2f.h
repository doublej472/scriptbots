#ifndef VEC2F_H
#define VEC2F_H
#include <stddef.h>

struct Vector2f {
  float x, y;
};

// Inline hot functions for cross-TU inlining
static inline float vector2f_dist2(struct Vector2f *vec1, struct Vector2f *vec2) {
  float dx = vec1->x - vec2->x;
  float dy = vec1->y - vec2->y;
  return dx * dx + dy * dy;
}

static inline void vector2f_add(struct Vector2f *dest, struct Vector2f *vec1, struct Vector2f *vec2) {
  dest->x = vec1->x + vec2->x;
  dest->y = vec1->y + vec2->y;
}

static inline void vector2f_sub(struct Vector2f *dest, struct Vector2f *vec1, struct Vector2f *vec2) {
  dest->x = vec1->x - vec2->x;
  dest->y = vec1->y - vec2->y;
}

void vector2f_init(struct Vector2f *vec, float x, float y);
float vector2f_dist(struct Vector2f *vec1, struct Vector2f *vec2);
float vector2f_length(struct Vector2f *vec);
float vector2f_angle(struct Vector2f *vec);
float vector2f_angle_between(struct Vector2f *vec1, struct Vector2f *vec2);
void vector2f_rotate(struct Vector2f *vec, float rads);

#endif
