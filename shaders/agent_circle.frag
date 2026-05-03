#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in flat uint fragType;
layout(location = 2) in flat float fragIndicatorSize;
layout(location = 0) out vec4 outColor;

void main() {
    if (fragType == 2u && fragIndicatorSize <= 0.0)
        discard;
    outColor = vec4(fragColor, 1.0);
}
