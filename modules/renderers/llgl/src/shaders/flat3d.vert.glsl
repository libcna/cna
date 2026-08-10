// SPDX-License-Identifier: MS-PL
// Flat (untextured, unlit, no vertex-colour attribute) 3D vertex shader, Vulkan flavour (SPIR-V).
//
// LLGL-52: the one 3D vertex-layout permutation this renderer previously refused outright --
// TextureEnabled=false, VertexColorEnabled=false/no colour attribute, lighting off. A flat,
// constant-DiffuseColor-only draw (every ModelMeshPart without a texture or per-vertex colour)
// needs a shader with NO colour attribute declared at all, unlike colored3d.vert.glsl's own
// location-1 `color` input -- handing that shader a pipeline whose vertex layout has no location-1
// entry to describe is what threw before this shader existed.
//
// The uniform block is the one described in effect3d_common.glsl.inc; every 3D shader here shares
// it byte for byte, so a change to one must change all of them and FillEffectUniforms together.
// Pairs with untextured3d.frag.glsl unchanged: that fragment stage only ever reads vColor/
// vTexCoord/vFogFactor, and does not care which vertex shader produced them.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    // Unused here (there is no vertex colour attribute to gate), but must stay present so this
    // shader's Transform block byte-matches every other unlit 3D shader it can be linked with in
    // the same OpenGL program -- see colored3d.vert.glsl for the field this backs.
    vec4 vertexColorEnabledPad;
};

layout(location = 0) in vec3 position;

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
    vTexCoord   = vec2(0.0);
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
