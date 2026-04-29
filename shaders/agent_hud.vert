#version 450

layout(location = 0) in vec2 vPos;
layout(location = 1) in uint vType;

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
    uint agentOffset;
} pc;

layout(location = 0) out flat uint fragType;
layout(location = 1) out float fragHerbivore;
layout(location = 2) out float fragSoundmul;

void main() {
    Agent a = agents[gl_InstanceIndex + pc.agentOffset];
    float xo = 18.0;
    float yo = -15.0;

    vec2 off;
    vec2 size;

    if (vType <= 1u) {
        off  = vec2(xo, yo);
        size = vec2(5.0, 40.0);
    } else if (vType == 2u) {
        off  = vec2(xo + 6.0, yo);
        size = vec2(6.0, 10.0);
    } else {
        off  = vec2(xo + 6.0, yo + 12.0);
        size = vec2(6.0, 10.0);
    }

    vec2 world = a.pos + off + vPos * size;

    if (vType == 1u) {
        world.y = a.pos.y + yo + size.y * (a.health / 2.0) * vPos.y;
    }

    gl_Position = cam.mvp * vec4(world, 0.0, 1.0);
    fragType      = vType;
    fragHerbivore = a.herbivore;
    fragSoundmul  = a.soundmul;
}
