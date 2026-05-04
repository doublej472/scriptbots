#version 450

// No vertex buffer — vertex positions computed from gl_VertexIndex
// covering the entire world from (0,0) to (worldW, worldH)
layout(set = 0, binding = 0) uniform Camera {
    mat4 mvp;
} cam;

layout(push_constant) uniform PushConst {
    float worldW, worldH;
    float cellSize;
    float foodMax;
} pc;

// Pass food-grid coordinates to fragment shader (flat — same per quad)
layout(location = 0) flat out float f_cellSize;
layout(location = 1) flat out ivec2 f_gridDim;
layout(location = 3) flat out float f_foodMax;
layout(location = 4) out vec2 worldPos;

void main() {
    vec2 pos;
    switch (gl_VertexIndex) {
        case 0: pos = vec2(0.0, 0.0); break;
        case 1: pos = vec2(pc.worldW, 0.0); break;
        case 2: pos = vec2(pc.worldW, pc.worldH); break;
        case 3: pos = vec2(0.0, 0.0); break;
        case 4: pos = vec2(pc.worldW, pc.worldH); break;
        case 5: pos = vec2(0.0, pc.worldH); break;
        default: pos = vec2(0.0);
    }
    worldPos = pos;
    f_cellSize = pc.cellSize;
    f_gridDim = ivec2(int(pc.worldW / pc.cellSize), int(pc.worldH / pc.cellSize));
    f_foodMax = pc.foodMax;
    gl_Position = cam.mvp * vec4(pos, 0.0, 1.0);
}
