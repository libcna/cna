#version 450

// Stride 32: VertexPositionNormalTexture — float3 pos + float3 normal + float2 uv
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;  // world-space (approximated from MVP upper-3x3)
layout(location = 2) out vec4  fragTint;

// 128-byte push constant block (all 3D variants share this layout).
layout(push_constant) uniform PC {
    mat4  mvp;               // offset   0, 64 bytes
    vec4  diffuseColor;      // offset  64, 16 bytes
    vec3  ambientColor;      // offset  80
    float lightingEnabled;   // offset  92
    vec3  light0Dir;         // offset  96
    float textureEnabled;    // offset 108
    vec3  light0Diffuse;     // offset 112
    float vertexColorEnabled;// offset 124
} pc;                        // total: 128 bytes

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    fragUV     = inUV;
    // Approximate world-space normal using upper-left 3x3 of MVP.
    // Valid when world transform has no non-uniform scaling.
    fragNormal = normalize(mat3(pc.mvp) * inNormal);
    fragTint   = pc.diffuseColor;
}
