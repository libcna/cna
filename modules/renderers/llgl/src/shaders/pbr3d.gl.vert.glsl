// SPDX-License-Identifier: MS-PL
// PbrEffect vertex shader, OpenGL flavour. See pbr3d.vert.glsl for the Vulkan flavour and the
// reasoning behind this shader's own dedicated (not shared) uniform block.

#version 450 core

layout(std140, binding = 1) uniform PbrParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 ambientColorPad;
    vec4 emissiveMetallic;
    vec4 roughnessWeightsPad;
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

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 normal;
layout(location = 6) in vec4 tangent;

layout(location = 0) out vec2  vTexCoord;
layout(location = 1) out vec3  vNormal;
layout(location = 2) out vec3  vTangent;
layout(location = 3) out float vBitangentSign;
layout(location = 4) out vec3  vWorldPos;
layout(location = 5) out float vFogFactor;

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vTexCoord   = texCoord;

    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal        = normalize(normalMatrix * normal);
    vTangent       = mat3(worldMatrix) * tangent.xyz;
    vBitangentSign = tangent.w;
    vWorldPos      = (worldMatrix * vec4(position, 1.0)).xyz;
    vFogFactor     = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
