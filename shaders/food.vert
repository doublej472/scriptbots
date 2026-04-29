#version 450

layout(location = 0) in vec2 vPos;
layout(location = 1) in vec3 vColor;

layout(set = 0, binding = 0) uniform Camera {
    mat4 mvp;
} cam;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = cam.mvp * vec4(vPos, 0.0, 1.0);
    fragColor = vColor;
}
