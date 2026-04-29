#version 450

layout(location = 0) in vec3 vPos;

layout(set = 0, binding = 0) uniform Camera {
    mat4 mvp;
} cam;

struct Agent {
    vec2  pos;
    float color_r, color_g, color_b;
    float angle;
    float health;
    float herbivore;
    float soundmul;
    float spikeLen;
    int   boost;
    int   selectFlag;
    float indicator_r, indicator_g, indicator_b;
    float indicatorSize;
};

layout(set = 0, binding = 1, std430) readonly buffer AgentBuf {
    Agent agents[];
};

layout(push_constant) uniform PushConst {
    float coneLength;
    float spikeScale;
    uint  agentOffset;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    Agent a = agents[gl_InstanceIndex + pc.agentOffset];
    float c = cos(a.angle);
    float s = sin(a.angle);
    float len = (vPos.z < 0.5) ? pc.coneLength : (pc.spikeScale * a.spikeLen);
    vec2 world = vec2(
        a.pos.x + len * (vPos.x * c - vPos.y * s),
        a.pos.y + len * (vPos.x * s + vPos.y * c)
    );
    gl_Position = cam.mvp * vec4(world, 0.0, 1.0);
    fragColor = (vPos.z < 0.5) ? vec3(0.5, 0.5, 0.5) : vec3(0.5, 0.0, 0.0);
}
