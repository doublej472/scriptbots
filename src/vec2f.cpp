#include <math.h>

#include "include/vec2f.h"

void vector2f_init(Vector2f& vec) {
  vec.x = 0.0f;
  vec.y = 0.0f;
}

void vector2f_init(Vector2f& vec, float x, float y) {
  vec.x = x;
  vec.y = y;
}

float vector2f_length(Vector2f& vec) {
  return sqrt(pow(vec.x, 2) + pow(vec.y, 2));
}

float vector2f_dist(Vector2f& vec1, Vector2f& vec2) {
  return sqrt(pow(vec2.x - vec1.x, 2) + pow(vec2.y - vec1.y, 2));
}

float vector2f_dist2(Vector2f& vec1, Vector2f& vec2) {
  return pow(vec2.x - vec1.x, 2) + pow(vec2.y - vec1.y, 2);
}

float vector2f_angle(Vector2f& vec) {
  if ((vec.x == 0.0f ) && ( vec.y == 0.0f )) {
    return 0.0f;
  }
  return atan2f(vec.y, vec.x);
}

float vector2f_angle_between(Vector2f& vec1, Vector2f& vec2) {
  Vector2f tmp;
  vector2f_sub(tmp, vec2, vec1);
  return vector2f_angle(tmp);
}

void vector2f_rotate(Vector2f& vec, float rads) {
  float mag = vector2f_length(vec);
  float rot = vector2f_angle(vec);
  rot += rads;
  vec.x = cos(rot) * mag;
  vec.y = sin(rot) * mag;
}

void vector2f_add(Vector2f& dest, Vector2f& vec1, Vector2f& vec2) {
  dest.x = vec1.x + vec2.x;
  dest.y = vec1.y + vec2.y;
}

void vector2f_sub(Vector2f& dest, Vector2f& vec1, Vector2f& vec2) {
  dest.x = vec1.x - vec2.x;
  dest.y = vec1.y - vec2.y;
}
