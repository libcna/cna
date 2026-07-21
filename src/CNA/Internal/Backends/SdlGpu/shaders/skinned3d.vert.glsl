#version 450

// Stride 52: VertexPositionNormalTextureSkinned -- float3 pos + float3 normal + float2 uv +
// float4 blendWeight + ubyte4 blendIndices (non-normalized, read as uvec4).
layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in vec4  inBoneWeights;
layout(location = 4) in uvec4 inBoneIndices;

// Interface matches lit_textured3d.frag.glsl's inputs exactly (same locations/types) -- this
// shader's fragment stage IS lit_textured3d's fragment shader, reused unchanged; SkinnedEffect's
// lighting math is identical to BasicEffect's lit path once the position/normal are skinned.
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragTint;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec4 fragFog;    // REMED-GFX-009 xyz=FogColor, w=keep-factor

// 72 * mat4 = 4608 bytes -- empirically found (via this shader's own real skinning-math test,
// SdlGpu_Skinned, binary-searching bone indices) that SDL_gpu's push-uniform-data mechanism has a
// real 4096-byte cap per slot on this Vulkan-backed environment (data past that offset silently
// did not update, unlike a graceful error) -- NOT documented in SDL_gpu.h, and well past what a
// hardware push-constant register file would allow anyway. A storage buffer
// (SDL_BindGPUVertexStorageBuffers) has no such size constraint, so the full 72-bone palette lives
// there instead of a uniform-buffer push. Per SDL_CreateGPUShader's binding convention, vertex-stage
// storage buffers are set 0 (after any sampled/storage textures, of which this shader has none).
layout(std430, set = 0, binding = 0) readonly buffer BoneBlock {
    mat4 bones[72];
} bb;

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

// Mirrors lit_textured3d.vert.glsl's own LitLightParams shape exactly (byte-identical layout),
// which is exactly why the fragment shader can be reused unchanged -- the only addition is
// WeightsPerVertex, packed into eyePos_weightsPerVertex.w (mirrors VulkanGraphicsBackend's own
// skinned3d.vert.glsl packing this into otherwise-unused padding).
layout(set = 1, binding = 1) uniform SkinnedLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;
    vec4 eyePos_weightsPerVertex;  // w = WeightsPerVertex
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;
} lp;

// REMED-GFX-009: fog forwarded to the fragment stage as a varying (the shared PC block is fully
// packed, no spare bytes). Keep-factor computed from raw object-space Z, matching
// VulkanGraphicsBackend's FogParams shape byte-for-byte.
layout(set = 1, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;      // x = FogStart, y = FogEnd, zw = unused
} fog;

void main() {
    // Matches VulkanGraphicsBackend's own skinned3d.vert.glsl: FNA's real Skin(vin, boneCount)
    // only sums the first WeightsPerVertex (1, 2, or 4) weight/index pairs.
    float weightsPerVertex = lp.eyePos_weightsPerVertex.w;
    mat4 skinMat = bb.bones[inBoneIndices.x] * inBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bb.bones[inBoneIndices.y] * inBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bb.bones[inBoneIndices.z] * inBoneWeights.z
                                          + bb.bones[inBoneIndices.w] * inBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    gl_Position = pc.mvp * skinnedPos;
    fragUV = inUV;
    // REMED-GFX-006: FNA composes the bone-skin 3x3 with the outer world normal matrix
    // (SkinnedEffect.fx Skin() then Lighting.fxh's mul(normal, WorldInverseTranspose)). The world
    // factor was missing entirely (audit Variant A), so any rotated or non-uniformly-scaled skinned
    // model was lit as if World were identity. Computed in-shader as transpose(inverse(mat3(world)))
    // because lp.world is already present and this backend mirrors VulkanGraphicsBackend's own
    // (now-fixed) skinned3d.vert.glsl.
    mat3 skinNormalMatrix = transpose(inverse(mat3(lp.world)));
    fragNormal = normalize(skinNormalMatrix * (mat3(skinMat) * inNormal));
    fragWorldPos = (lp.world * skinnedPos).xyz;
    fragTint = pc.diffuseColor;
    // REMED-GFX-009: keep-factor from raw object-space Z (GFX-005 corrected form
    // (z+FogEnd)/(FogEnd-FogStart)); FogStart==FogEnd -> fully fogged (FNA SetFogVector). keep=1 ->
    // no fog, keep=0 -> full FogColor. Skinned shaders use the PRE-skin inPos.z (matches Vulkan).
    float fogKeep = (fog.fogColorEnabled.w > 0.5)
        ? ((abs(fog.fogStartEnd.y - fog.fogStartEnd.x) < 1e-6) ? 0.0
            : clamp((inPos.z + fog.fogStartEnd.y) / (fog.fogStartEnd.y - fog.fogStartEnd.x), 0.0, 1.0))
        : 1.0;
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
