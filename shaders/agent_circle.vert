#version 450

layout(location = 0) in vec2 vPos;

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
    float botRadius;
    uint  agentOffset;
    uint  type;       // 0 = body, 1 = selection, 2 = indicator event
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out flat uint fragType;
layout(location = 2) out flat float fragIndicatorSize;

void main() {
    Agent a = agents[gl_InstanceIndex + pc.agentOffset];
    float c = cos(a.angle);
    float s = sin(a.angle);
    float radius = pc.botRadius;
    if (pc.type == 2u)
        radius += a.indicatorSize;  // event magnitude scales the ring
    vec2 world = vec2(
        a.pos.x + radius * (vPos.x * c - vPos.y * s),
        a.pos.y + radius * (vPos.x * s + vPos.y * c)
    );
    gl_Position = cam.mvp * vec4(world, 0.0, 1.0);

    fragType = pc.type;
    fragIndicatorSize = a.indicatorSize;

    if (pc.type == 2u) {
        fragColor = vec3(a.indicator_r, a.indicator_g, a.indicator_b);
    } else {
        fragColor = vec3(a.color_r, a.color_g, a.color_b);
    }
}
