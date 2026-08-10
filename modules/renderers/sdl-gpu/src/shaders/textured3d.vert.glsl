#version 450

// Stride 20 (VertexPositionTexture: float3 pos + float2 uv) *or* stride 24
// (VertexPositionColorTexture: float3 pos + ubyte4 color + float2 uv) -- see
// colored_textured3d.vert.glsl for the stride-24 variant; this one is stride 20 only
// (no vertex color attribute).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor

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
