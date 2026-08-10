#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

layout(push_constant) uniform PC {
    vec2 viewportSize;
} pc;

void main() {
    // pixel-space → NDC: (0,0) top-left, Y-down to match XNA
    vec2 ndc = (inPos / pc.viewportSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragUV    = inUV;
    fragColor = inColor;
}
