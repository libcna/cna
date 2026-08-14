// SPDX-License-Identifier: MS-PL
// SkinnedPbrEffect vertex shader, Vulkan flavour (SPIR-V).
//
// Combines pbr3d.vert.glsl's TBN-basis output with skinned3d.vert.glsl's own weightsPerVertex-
// gated bone blend. Shares the EXACT SAME PbrParams uniform block (and binding number) as the
// unskinned pbr3d.vert.glsl/frag.glsl -- WeightsPerVertex was already reserved as
// roughnessWeightsPad.y ("unused by this shader" there) precisely so this variant needs no shape
// change. BoneBlock is placed at a HIGHER binding (12) than every texture/sampler pair (2-11)
// rather than shifting them, so primitivePbrFragmentShader_ -- compiled against those exact
// binding numbers -- can be reused verbatim for both the skinned and unskinned pipelines; only the
// vertex shader differs.

#version 450

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 ambientColorPad;
    vec4 emissiveMetallic;
    vec4 roughnessWeightsPad;  // x = RoughnessFactor, y = WeightsPerVertex
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 eyePositionWorldPad;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
};

layout(std140, binding = 12) uniform BoneBlock
{
    mat4 bones[72];
};

layout(location = 0) in vec3  position;
layout(location = 2) in vec2  texCoord;
layout(location = 3) in vec3  normal;
layout(location = 4) in vec4  aBoneWeights;
layout(location = 5) in uvec4 aBoneIndices;
layout(location = 6) in vec4  tangent;

layout(location = 0) out vec2  vTexCoord;
layout(location = 1) out vec3  vNormal;
layout(location = 2) out vec3  vTangent;
layout(location = 3) out float vBitangentSign;
layout(location = 4) out vec3  vWorldPos;
layout(location = 5) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    // FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs -- matches skinned3d.vert.glsl's own established gating.
    float weightsPerVertex = roughnessWeightsPad.y;
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bones[aBoneIndices.z] * aBoneWeights.z
                                          + bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(position, 1.0);
    gl_Position = mvpMatrix * skinnedPos;
    vTexCoord   = texCoord;

    // Bone-skin 3x3 composed with the outer world inverse-transpose normal matrix, mirroring
    // skinned3d.vert.glsl's own REMED-GFX-006 composition order.
    mat3 skinNormalMat = mat3(skinMat);
    mat3 worldNormalMat = transpose(inverse(mat3(worldMatrix)));
    vNormal        = normalize(worldNormalMat * (skinNormalMat * normal));
    // Tangent: skin then raw world (directions transform differently from normals), matching
    // pbr3d.vert.glsl's own documented simplification for the unskinned case.
    vTangent       = mat3(worldMatrix) * (skinNormalMat * tangent.xyz);
    vBitangentSign = tangent.w;
    vWorldPos      = (worldMatrix * skinnedPos).xyz;
    // Fog factor from the POST-skin position, matching skinned3d.vert.glsl's own convention.
    vFogFactor     = clamp(dot(skinnedPos, fogVector), 0.0, 1.0) * fogColor.a;
}
