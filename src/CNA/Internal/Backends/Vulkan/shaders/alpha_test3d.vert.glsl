#version 450

// AlphaTestEffect vertex shader.
// Reads position (location=0) and UV (location=1).
// UV offset varies per stride; VkVertexInputAttributeDescription handles the mapping:
//   stride=20: UV at byte offset 12
//   stride=24: UV at byte offset 16  (past the 4-byte color)
//   stride=32: UV at byte offset 24  (past the 3-float normal)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

layout(push_constant) uniform PC {
    mat4  mvp;          // offset  0  (64 bytes)
    vec4  diffuseColor; // offset 64  (16 bytes)
    // [80..127] alphaTest params + padding — not read in VS
} pc;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    fragUV   = inUV;
    fragTint = pc.diffuseColor;
}
