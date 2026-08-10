// SPDX-License-Identifier: MS-PL
// SkinnedEffect.VertexColorEnabled vertex shader, Vulkan flavour (SPIR-V). LLGL-37, CNAEXT extension
// property (real XNA's SkinnedEffect has no such property; CNB-66/67 added it for glTF COLOR_0
// import support, matching every other renderer). Selected instead of skinned3d.vert.glsl only when
// the bound vertex buffer's own layout carries a colour attribute (stride 56, colour appended at
// offset 52) -- see LlglRenderer.cpp's own AcquirePrimitiveSkinnedVertexShader() doc comment
// for why this is a SEPARATE shader file rather than an always-declared, conditionally-read
// attribute the way EasyGL's single shared shader does it: this renderer selects a compiled shader
// variant per vertex layout SHAPE everywhere else (BasicEffect's own colored3d.vert.glsl vs
// textured3d.vert.glsl split is the established precedent), and a shader declaring an input the
// bound buffer does not supply reads undefined data on Vulkan.
//
// Otherwise identical to skinned3d.vert.glsl -- see that file's own doc comment for the
// SkinnedParams/BoneBlock uniform blocks and the skinning/fog math, both unchanged here.

#version 450

layout(std140, binding = 1) uniform SkinnedParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    // .xyz = EmissiveColor (pre-folded with ambient, see skinned3d.frag.glsl's own comment); .w >
    // 0.5 when SkinnedEffect.VertexColorEnabled is true -- the same free-slot reuse trick
    // BasicEffect's own ambientColorLighting.w uses, since SkinnedEffect has no separate ambient
    // term of its own to occupy this vec4's fourth component.
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
layout(location = 1) in vec4  color;
layout(location = 2) in vec2  texCoord;
layout(location = 3) in vec3  normal;
layout(location = 4) in vec4  aBoneWeights;
layout(location = 5) in uvec4 aBoneIndices;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out float vFogFactor;
layout(location = 4) out vec4 vColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

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
    vColor      = color;
}
