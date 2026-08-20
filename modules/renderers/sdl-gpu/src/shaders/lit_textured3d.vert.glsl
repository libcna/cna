#version 450

// Stride 32: VertexPositionNormalTexture -- float3 pos + float3 normal + float2 uv.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec3  fragNormal;
layout(location = 2) out vec4  fragTint;
layout(location = 3) out vec3  fragWorldPos;
layout(location = 4) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor

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

// plans/plan_sdlgpu.md: DirectionalLight1/DirectionalLight2 + EmissiveColor + specular + the World
// matrix (needed here to compute a world-space normal/position), forwarded via a second
// vertex-stage UBO (set 1, binding 1) since the primary 128-byte UBO above is already fully
// packed -- mirrors VulkanRenderer/WebGPURenderer's own lit_textured3d second-UBO
// precedent (field names kept identical to those renderers' for easier cross-reference).
layout(set = 1, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;
    vec4 eyePos_pad;
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;
} lp;

// REMED-GFX-009: fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// VulkanRenderer's FogParams shape byte-for-byte.
layout(set = 1, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;        // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragUV = inUV;
    // GLSL has a built-in inverse(), unlike WGSL -- no need for WebGPU's CPU-precomputed normal
    // matrix workaround; this mirrors VulkanRenderer's own lit_textured3d.vert.glsl exactly.
    mat3 normalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragWorldPos = (lp.world * vec4(inPos, 1.0)).xyz;
    fragTint = pc.diffuseColor;
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = 1.0 - clamp(dot(vec4(inPos, 1.0), fog.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
