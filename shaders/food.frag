#version 450

layout(set = 0, binding = 1, std430) readonly buffer FoodData {
    float data[];
} food;

layout(location = 0) flat in float f_cellSize;
layout(location = 1) flat in ivec2 f_gridDim;
layout(location = 3) flat in float f_foodMax;
layout(location = 4) in vec2 worldPos;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 cell = ivec2(floor(worldPos / f_cellSize));
    cell = clamp(cell, ivec2(0, 0), f_gridDim - ivec2(1, 1));
    int idx = cell.y * f_gridDim.x + cell.x;
    float amt = food.data[idx];
    if (amt <= 0.0) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float f = amt / f_foodMax;
    float g = 0.02 + f * 0.8;
    outColor = vec4(0.02, g, 0.02, 1.0);
}
