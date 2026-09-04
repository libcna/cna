// SPDX-License-Identifier: MS-PL
// Lit, textured 3D vertex shader with NO normal attribute, Vulkan flavour (SPIR-V).
//
// LLGL-52: a lit+textured draw from a vertex layout with no normal (e.g. plain
// VertexPositionTexture) previously threw outright rather than draw. Every stock lit-textured
// shader elsewhere in this renderer takes its normal from a `layout(location = 3) in vec3 normal`
// attribute; this variant has none to read, so it substitutes a fixed (0, 0, 1) object-space
// normal (transformed by the same world normal matrix as lit_textured3d.vert.glsl, so lighting
// still responds to World rotation) instead of refusing the draw. Otherwise identical to
// lit_textured3d.vert.glsl -- pairs with lit_textured3d.frag.glsl unchanged.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    mat4 worldMatrix;
    vec4 ambientColorLighting;
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light0SpecularPad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light1SpecularPad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 light2SpecularPad;
    vec4 emissiveColorPad;
    vec4 eyePositionWorldPad;
    vec4 specularColorPower;
};

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec4 vTint;
layout(location = 4) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vTexCoord   = texCoord;

    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal     = normalize(normalMatrix * vec3(0.0, 0.0, 1.0));
    vWorldPos   = (worldMatrix * vec4(position, 1.0)).xyz;
    vTint       = diffuseColor;
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
