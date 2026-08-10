// SPDX-License-Identifier: MS-PL
// SkinnedEffect vertex shader, OpenGL flavour. See skinned3d.vert.glsl for the Vulkan flavour and
// the reasoning behind this shader's own dedicated (not shared) uniform blocks.

#version 450 core

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
    vec4 eyePositionWorldWeights;
    vec4 specularColorPower;
    vec4 fogColor;
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

void main()
{
    float weightsPerVertex = eyePositionWorldWeights.w;
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x;
    if (weightsPerVertex >= 2.0) skinMat += bones[aBoneIndices.y] * aBoneWeights.y;
    if (weightsPerVertex >= 4.0) skinMat += bones[aBoneIndices.z] * aBoneWeights.z
                                          + bones[aBoneIndices.w] * aBoneWeights.w;
    vec4 skinnedPos = skinMat * vec4(position, 1.0);
    gl_Position = mvpMatrix * skinnedPos;
    vTexCoord   = texCoord;

    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal     = normalize(normalMatrix * (mat3(skinMat) * normal));
    vWorldPos   = (worldMatrix * skinnedPos).xyz;
    vFogFactor  = clamp(dot(skinnedPos, fogVector), 0.0, 1.0) * fogColor.a;
}
