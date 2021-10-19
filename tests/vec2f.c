#include "include/vec2f.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

void _assert_eq(const char* file, int line, float f1, float f2) {
  if (f1 - f2 < 0.00001f) {
    return;
  } else {
    printf("assert_eq failed!\n");
    printf("%s:%i\n", file, line);
    printf("f1=%f\n", f1);
    printf("f2=%f\n", f2);
    exit(-1);
  }
}

#define assert_eq(f1, f2) _assert_eq(__FILE__, __LINE__, f1, f2)

void test_vec2f() {
  struct Vector2f v1, v2, v3;

  vector2f_init(&v1, 0.0f, 0.0f);
  assert_eq(v1.x, 0.0f);
  assert_eq(v1.y, 0.0f);

  vector2f_init(&v2, 5.0f, -2.0f);
  assert_eq(v2.x, 5.0f);
  assert_eq(v2.y, -2.0f);

  v1.x = 1.0f;
  v1.y = 5.0f;

  vector2f_add(&v3, &v1, &v2);
  assert_eq(v3.x, 6.0f);
  assert_eq(v3.y, 3.0f);

  vector2f_sub(&v3, &v1, &v2);
  assert_eq(v3.x, -4.0f);
  assert_eq(v3.y, 7.0f);

  v1.x = 3.0f;
  v1.y = 4.0f;

  assert_eq(vector2f_length(&v1), 5.0f);

  v2.x = 1.0f;
  v2.y = 6.0f;

  assert_eq(vector2f_dist2(&v1, &v2), 8.0f);
  assert_eq(vector2f_dist(&v1, &v2), 2.8284271247461903f);

  v3.x = 0.0f;
  v3.y = 0.0f;

  assert_eq(vector2f_angle(&v3), 0.0f);

  v1.x = -1.0f;
  v1.y = 1.0f;

  v2.x = 0.0f;
  v2.y = 1.0f;

  assert_eq(vector2f_angle(&v1), (3*M_PI)/4);
  assert_eq(vector2f_angle_between(&v1, &v2), M_PI/4);

  vector2f_rotate(&v2, M_PI/2);
  assert_eq(v2.x, -1.0f);
  assert_eq(v2.y, 0.0f);
}

int main() {
  printf("Testing vector operations...\n");
  test_vec2f();
  printf("Vector2f tests successful!\n");
  return 0;
}
