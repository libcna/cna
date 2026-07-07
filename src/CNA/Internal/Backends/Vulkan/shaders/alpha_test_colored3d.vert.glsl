#version 450

// AlphaTestEffect vertex shader, stride-24 (VertexPositionColorTexture) variant.
// Task 887: unlike alpha_test3d.vert.glsl (strides 20/32, no color attribute), this variant
// reads the vertex color and gates its multiply by VertexColorEnabled, mirroring
// colored_textured3d.vert.glsl's already-correct BasicEffect formula.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;   // normalized UNORM R8G8B8A8
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragFogFactor;

layout(push_constant) uniform PC {
    mat4  mvp;                 // offset  0  (64 bytes)
    vec4  diffuseColor;        // offset 64  (16 bytes)
    vec4  alphaTestParams;     // offset 80  (16 bytes) — unused here, read only in the FS
    float vertexColorEnabled;  // offset 96
    // Task 888: fog, packed into what was previously unused padding.
    float fogEnabled;          // offset 100
    float fogStart;            // offset 104
    float fogEnd;              // offset 108
    vec3  fogColor;            // offset 112
} pc;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    fragUV   = inUV;
    fragTint = (pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;
    // Task 888: fog factor from raw object-space Z (matches EasyGL's established formula
    // exactly). 1.0 = no fog, 0.0 = full fog.
    fragFogFactor = (pc.fogEnabled > 0.5)
        ? clamp((pc.fogEnd - inPos.z) / max(pc.fogEnd - pc.fogStart, 1e-6), 0.0, 1.0)
        : 1.0;
}
