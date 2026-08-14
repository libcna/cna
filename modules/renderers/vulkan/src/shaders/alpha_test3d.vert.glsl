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
layout(location = 2) out float fragFogFactor;

layout(push_constant) uniform PC {
    mat4  mvp;                 // offset  0  (64 bytes)
    vec4  diffuseColor;        // offset 64  (16 bytes)
    vec4  alphaTestParams;     // offset 80  (16 bytes) — unused here, read only in the FS
    float vertexColorEnabled;  // offset 96  — unused here (this variant has no color attribute)
    // REMED-GFX-010: FogColor as 3 floats (100/104/108) + the FNA fog vector as a 16-byte-aligned
    // vec4 at offset 112 (reuses fogColor's old slot; fog vector .w lands in the old vec3 padding).
    float fogColorR;           // offset 100
    float fogColorG;           // offset 104
    float fogColorB;           // offset 108
    vec4  fogVector;           // offset 112 — FNA fog vector (dot with object-space position)
} pc;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV   = inUV;
    fragTint = pc.diffuseColor;
    // Task 888: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL
    // Task-1111 form (z+FogEnd)/(FogEnd-FogStart); prior (FogEnd-z) was the mirror image and
    // wrong. 1.0 = no fog, 0.0 = full fog. Zero-length range -> fully fogged (FNA parity).
    // REMED-GFX-010: view-space fog = 1 - saturate(dot(objectPos, fogVector)).
    fragFogFactor = 1.0 - clamp(dot(vec4(inPos, 1.0), pc.fogVector), 0.0, 1.0);
}
