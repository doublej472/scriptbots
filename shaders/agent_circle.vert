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
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    Agent a = agents[gl_InstanceIndex + pc.agentOffset];
    float c = cos(a.angle);
    float s = sin(a.angle);
    vec2 world = vec2(
        a.pos.x + pc.botRadius * (vPos.x * c - vPos.y * s),
        a.pos.y + pc.botRadius * (vPos.x * s + vPos.y * c)
    );
    gl_Position = cam.mvp * vec4(world, 0.0, 1.0);
    fragColor = vec3(a.color_r, a.color_g, a.color_b);
}
