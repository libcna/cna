// SPDX-License-Identifier: MS-PL
// Lit, coloured, UNTEXTURED 3D vertex shader, Vulkan flavour (SPIR-V) (LLGL-31). Identical to
// lit_colored_textured3d.vert.glsl minus the texture coordinate attribute/output -- see that file
// for the uniform block's shape and why it is shared byte for byte across every 3D shader here.
//
// Attribute locations still follow the renderer's usage-to-location mapping (position 0, colour 1,
// texture coordinate 2, normal 3): this variant simply never declares location 2, the same way
// colored3d.vert.glsl (the unlit sibling) does.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    mat4 worldMatrix;
    // .w > 0.5 when the effect's VertexColorEnabled is true (reuses this vec4's otherwise-unused
    // fourth component -- ambientColorLighting itself is an RGB colour); otherwise the colour
    // attribute below must NOT multiply into the tint, even though the vertex layout carries one.
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
layout(location = 1) in vec4 color;
layout(location = 3) in vec3 normal;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec4 vTint;
layout(location = 3) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal     = normalize(normalMatrix * normal);
    vWorldPos   = (worldMatrix * vec4(position, 1.0)).xyz;
    vTint       = (ambientColorLighting.w > 0.5) ? diffuseColor * color : diffuseColor;
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
