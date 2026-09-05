#version 450

// Stride 20: VertexPositionTexture — float3 pos + float2 uv
//
// Task 899: dedicated vertex shader (previously DualTextureEffect reused textured3d.vert.glsl's
// compiled SPIR-V directly). textured3d.vert.glsl now declares its own fog UBO at binding=1
// (the shared colored3d/textured3d/colored_textured3d bundle's layout), which conflicts with
// dual_texture3d's own 2-sampler descriptor set layout (extended here with its own fog UBO at
// binding=2, since bindings 0/1 are already the two texture samplers) -- so this pipeline needs
// its own vertex shader file, split off with identical MVP/diffuseColor logic.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
// plans/plan_vulkan.md VULKAN-150: DualTextureEffect's second sampler is addressed by
// TEXCOORD1, not by TEXCOORD0. Before this input existed the fragment shader sampled both
// textures with fragUV, so a declaration carrying an independent second UV set could not be
// honoured -- and the stride guard refusing such a record was the only thing hiding it.
// A record that declares no TextureCoordinate1 has this input pointed at TextureCoordinate0's
// own element, which reproduces the previous behaviour exactly.
layout(location = 2) in vec2 inUV1;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragFogFactor;
layout(location = 3) out vec2 fragUV1;

layout(push_constant) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    vec4 pos = pc.mvp * vec4(inPos, 1.0);
    pos.y = -pos.y;
    gl_Position = pos;
    gl_PointSize = 1.0;
    fragUV   = inUV;
    fragUV1  = inUV1;
    fragTint = pc.diffuseColor;
    // Task 899: fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL Task-1111
    // form (z+FogEnd)/(FogEnd-FogStart); the prior Task 888/899 (FogEnd-z) formula was the
    // mirror image and wrong. Zero-length range -> fully fogged, matching FNA SetFogVector.
    fragFogFactor = 1.0 - clamp(dot(vec4(inPos, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
}
