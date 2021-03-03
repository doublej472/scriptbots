#include <math.h>

#include "include/vec2f.h"

void vector2f_init(struct Vector2f *vec, float x, float y) {
  vec->x = x;
  vec->y = y;
}

float vector2f_length(struct Vector2f *vec) {
  return sqrt(vec->x*vec->x + vec->y*vec->y);
}

float vector2f_dist(struct Vector2f *vec1, struct Vector2f *vec2) {
  return sqrt(vector2f_dist2(vec1, vec2));
}

float vector2f_dist2(struct Vector2f *vec1, struct Vector2f *vec2) {
  return ((vec2->x - vec1->x) * (vec2->x - vec1->x))
    + ((vec2->y - vec1->y) * (vec2->y - vec1->y));
}

float vector2f_angle(struct Vector2f *vec) {
  if ((vec->x == 0.0f ) && ( vec->y == 0.0f )) {
    return 0.0f;
  }
  return atan2f(vec->y, vec->x);
}

float vector2f_angle_between(struct Vector2f *vec1, struct Vector2f *vec2) {
  struct Vector2f tmp;
  vector2f_sub(&tmp, vec2, vec1);
  return vector2f_angle(&tmp);
}

void vector2f_rotate(struct Vector2f *vec, float rads) {
  float mag = vector2f_length(vec);
  float rot = vector2f_angle(vec);
  rot += rads;
  vec->x = cos(rot) * mag;
  vec->y = sin(rot) * mag;
}

void vector2f_add(struct Vector2f *dest, struct Vector2f *vec1, struct Vector2f *vec2) {
  dest->x = vec1->x + vec2->x;
  dest->y = vec1->y + vec2->y;
}

void vector2f_sub(struct Vector2f *dest, struct Vector2f *vec1, struct Vector2f *vec2) {
  dest->x = vec1->x - vec2->x;
  dest->y = vec1->y - vec2->y;
}
