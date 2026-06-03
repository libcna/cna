#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

// XNA row-major MVP: apply as (pos * mvp) which equals mvp^T * pos in column-major
layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragColor   = inColor;
}
