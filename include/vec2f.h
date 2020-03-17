#ifndef VEC2F_H
#define VEC2F_H
struct Vector2f {
  float x, y;
};

void vector2f_init(Vector2f& vec);
void vector2f_init(Vector2f& vec, float x, float y);
float vector2f_dist(Vector2f& vec1, Vector2f& vec2);
float vector2f_dist2(Vector2f& vec1, Vector2f& vec2);
float vector2f_length(Vector2f& vec);
float vector2f_angle(Vector2f& vec);
float vector2f_angle_between(Vector2f& vec1, Vector2f& vec2);
void vector2f_rotate(Vector2f& vec, float rads);
void vector2f_add(Vector2f& dest, Vector2f& vec1, Vector2f& vec2);
void vector2f_sub(Vector2f& dest, Vector2f& vec1, Vector2f& vec2);

#endif
