#version 450

// Stride 20 (VertexPositionTexture: float3 pos + float2 uv) *or* stride 24
// (VertexPositionColorTexture: float3 pos + ubyte4 color + float2 uv) -- see
// colored_textured3d.vert.glsl for the stride-24 variant; this one is stride 20 only
// (no vertex color attribute).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

layout(set = 1, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTint = pc.diffuseColor;
}
