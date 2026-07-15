#version 450

// AlphaTestEffect vertex shader for strides 20 (VertexPositionTexture) and 32
// (VertexPositionNormalTexture, normal simply unread) -- the pipeline's vertex-input-state
// configures the correct per-stride attribute offsets; this shader only ever reads position+UV.
// Stride 24 (VertexPositionColorTexture) uses alpha_test_colored3d.vert.glsl instead.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;

// No fog (deliberately deferred, same as this codebase's WebGPU backend's own AlphaTestEffect port).
layout(set = 1, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    float alphaRef;
    float alphaTol;
    float alphaPassW;
    float alphaFailW;
    float vertexColorEnabled;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTint = pc.diffuseColor;
}
