#version 450

// AlphaTestEffect vertex shader for strides 20 (VertexPositionTexture) and 32
// (VertexPositionNormalTexture, normal simply unread) -- the pipeline's vertex-input-state
// configures the correct per-stride attribute offsets; this shader only ever reads position+UV.
// Stride 24 (VertexPositionColorTexture) uses alpha_test_colored3d.vert.glsl instead.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor

// Fog (REMED-GFX-009) is supplied via the FogParams block below.
layout(set = 1, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    float alphaRef;
    float alphaTol;
    float alphaPassW;
    float alphaFailW;
    float vertexColorEnabled;
} pc;

// REMED-GFX-009: fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// VulkanRenderer's FogParams shape byte-for-byte.
layout(set = 1, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;        // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    fragTint = pc.diffuseColor;
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = 1.0 - clamp(dot(vec4(inPos, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
