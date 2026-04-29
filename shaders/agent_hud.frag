#version 450

layout(location = 0) in flat uint fragType;
layout(location = 1) in float fragHerbivore;
layout(location = 2) in float fragSoundmul;

layout(location = 0) out vec4 outColor;

void main() {
    switch (fragType) {
    case 0u:  // health bar background
        outColor = vec4(0.5, 0.0, 0.0, 1.0);
        break;
    case 1u:  // health bar fill
        outColor = vec4(0.0, 0.8, 0.0, 1.0);
        break;
    case 2u:  // herbivore indicator
        outColor = vec4(1.0 - fragHerbivore, fragHerbivore, 0.0, 1.0);
        break;
    case 3u:  // sound indicator
        outColor = vec4(fragSoundmul, fragSoundmul, fragSoundmul, 1.0);
        break;
    default:
        outColor = vec4(1.0, 0.0, 1.0, 1.0);
    }
}
