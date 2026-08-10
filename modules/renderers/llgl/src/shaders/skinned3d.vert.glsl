// SPDX-License-Identifier: MS-PL
// SkinnedEffect vertex shader, Vulkan flavour (SPIR-V).
//
// Like EnvironmentMapEffect (see env_map3d.vert.glsl), this does NOT share the common `Transform`
// uniform block -- its field set (no alpha test, no separate ambient term -- SkinnedEffect
// pre-folds ambient into EmissiveColor exactly like EnvironmentMapEffect does, per
// SkinnedEffect::FillGpuDrawParams -- plus per-light specular and WeightsPerVertex) doesn't fit it,
// and this vertex/fragment pair is never linked with any other shader here.
//
// The bone transform array (72 mat4s, 4608 bytes) is a SEPARATE uniform block (BoneBlock) rather
// than folded into SkinnedParams: it is far larger than every other per-draw uniform block in this
// renderer, and giving it its own buffer/pool mirrors the Vulkan renderer's own BoneBlock/FogParams
// split (env_map3d takes the opposite, single-buffer approach only because its own field set is
// small enough to fit comfortably in one).

#version 450

layout(std140, binding = 1) uniform SkinnedParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 emissiveColorPad;
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light0SpecularPad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light1SpecularPad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 light2SpecularPad;
    vec4 eyePositionWorldWeights; // xyz = eye position, w = WeightsPerVertex (1, 2, or 4)
    vec4 specularColorPower;
    vec4 fogColor;                // xyz = FogColor, w = fogEnabled (0/1)
    vec4 fogVector;
};

layout(std140, binding = 2) uniform BoneBlock
{
    mat4 bones[72];
};

layout(location = 0) in vec3  position;
layout(location = 2) in vec2  texCoord;
layout(location = 3) in vec3  normal;
layout(location = 4) in vec4  aBoneWeights;
layout(location = 5) in uvec4 aBoneIndices;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    // FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating
    // suffices instead of a compile-time-unrolled per-bone-count shader variant.
    float weightsPerVertex = eyePositionWorldWeights.w;
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bones[aBoneIndices.z] * aBoneWeights.z
                                          + bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(position, 1.0);
    gl_Position = mvpMatrix * skinnedPos;
    vTexCoord   = texCoord;

    // FNA composes the bone-skin 3x3 with the outer world normal matrix (SkinnedEffect.fx's
    // Skin() then Lighting.fxh's mul(normal, WorldInverseTranspose)) -- dropping the world factor
    // would light any rotated or non-uniformly-scaled skinned model as if World were identity.
    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal     = normalize(normalMatrix * (mat3(skinMat) * normal));
    vWorldPos   = (worldMatrix * skinnedPos).xyz;
    // Fog factor from the POST-skin position, matching this renderer's own convention (0 = no fog,
    // see lit_textured3d.vert.glsl) -- not the Vulkan-standalone renderer's opposite convention.
    vFogFactor  = clamp(dot(skinnedPos, fogVector), 0.0, 1.0) * fogColor.a;
}
