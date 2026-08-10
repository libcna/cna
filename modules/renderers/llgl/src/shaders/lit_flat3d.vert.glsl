// SPDX-License-Identifier: MS-PL
// Lit, untextured 3D vertex shader with NO vertex-colour attribute, Vulkan flavour (SPIR-V).
//
// LLGL-52: a lit, untextured BasicEffect draw with VertexColorEnabled=false from a colourless
// vertex layout (e.g. VertexPositionNormalTexture, TextureEnabled=false, EnableDefaultLighting())
// previously fell through to the UNLIT flat3d shader instead -- lit_colored3d.vert.glsl (the
// existing lit+untextured variant) always declares a `color` attribute at location 1, which this
// combination's vertex buffer does not supply. Identical to lit_colored3d.vert.glsl minus the
// colour attribute/multiply -- pairs with lit_untextured3d.frag.glsl unchanged (its own inputs,
// vNormal/vWorldPos/vTint/vFogFactor, are exactly what this shader produces).

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    mat4 worldMatrix;
    // Unused here (there is no vertex colour attribute to gate), but must stay present so this
    // shader's Transform block byte-matches every other lit 3D shader it can be linked with in the
    // same OpenGL program -- see lit_colored3d.vert.glsl for the field this backs.
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
    vTint       = diffuseColor;
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
