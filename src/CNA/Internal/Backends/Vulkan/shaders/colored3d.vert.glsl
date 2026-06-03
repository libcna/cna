#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

// XNA row-major MVP: apply as (pos * mvp) which equals mvp^T * pos in column-major
layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;                      // Vulkan NDC Y is inverted vs OpenGL
    pos.z = (pos.z + pos.w) * 0.5;      // remap Z from [-w,+w] to [0,+w]
    gl_Position = pos;
    fragColor   = inColor;
}
