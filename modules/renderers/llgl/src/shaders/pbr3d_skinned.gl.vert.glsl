// SPDX-License-Identifier: MS-PL
// SkinnedPbrEffect vertex shader, OpenGL flavour. See pbr3d_skinned.vert.glsl for the Vulkan
// flavour and the reasoning behind BoneBlock's own binding number.

#version 450 core

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 ambientColorPad;
    vec4 emissiveMetallic;
    vec4 roughnessWeightsPad;  // x=roughness, y=skin weights, z=normal scale, w=occlusion strength
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

void main()
{
    float weightsPerVertex = roughnessWeightsPad.y;
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bones[aBoneIndices.z] * aBoneWeights.z
                                          + bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(position, 1.0);
    gl_Position = mvpMatrix * skinnedPos;
    vTexCoord   = texCoord;

    mat3 skinNormalMat = mat3(skinMat);
    mat3 worldNormalMat = transpose(inverse(mat3(worldMatrix)));
    vNormal        = normalize(worldNormalMat * (skinNormalMat * normal));
    vTangent       = mat3(worldMatrix) * (skinNormalMat * tangent.xyz);
    vBitangentSign = tangent.w;
    vWorldPos      = (worldMatrix * skinnedPos).xyz;
    vFogFactor     = clamp(dot(skinnedPos, fogVector), 0.0, 1.0) * fogColor.a;
}
