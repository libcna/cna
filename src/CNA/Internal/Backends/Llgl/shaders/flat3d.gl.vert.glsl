// SPDX-License-Identifier: MS-PL
// Flat (untextured, unlit, no vertex-colour attribute) 3D vertex shader, OpenGL flavour.
//
// See flat3d.vert.glsl (the Vulkan/SPIR-V twin) for the LLGL-52 rationale. Pairs with
// untextured3d.gl.frag.glsl unchanged.

#version 450 core

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    // Unused here (there is no vertex colour attribute to gate), but must stay present so this
    // shader's Transform block byte-matches every other unlit 3D shader it can be linked with in
    // the same OpenGL program -- see colored3d.gl.vert.glsl for the field this backs.
    vec4 vertexColorEnabledPad;
};

layout(location = 0) in vec3 position;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out float vFogFactor;

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vColor      = diffuseColor;
    vTexCoord   = vec2(0.0);
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
