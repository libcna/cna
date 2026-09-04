// SPDX-License-Identifier: MS-PL
// Textured 3D vertex shader, Vulkan flavour (SPIR-V).
//
// The uniform block is the one described in effect3d_common.glsl.inc; every 3D shader here shares
// it byte for byte, so a change to one must change all of them and FillEffectUniforms together.
// Attribute locations follow the renderer's usage-to-location mapping (position 0, colour 1,
// texture coordinate 2), which is why a layout without vertex colours simply has no location 1.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    // Unused here, but must stay present so this shader's Transform block byte-matches every
    // other unlit 3D shader it can be linked with in the same OpenGL program (GL requires an
    // identically named/laid-out uniform block to match exactly across linked stages) -- see
    // colored3d.vert.glsl for the field this backs.
    vec4 vertexColorEnabledPad;
};

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vColor      = diffuseColor;
    vTexCoord   = texCoord;
    // The fog factor is a view-space depth term folded into a single vector by the
    // effect layer, so dotting it with the object-space position is the whole
    // computation -- no separate view matrix is needed here.
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
