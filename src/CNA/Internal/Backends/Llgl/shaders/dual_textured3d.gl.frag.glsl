// SPDX-License-Identifier: MS-PL
// DualTextureEffect fragment shader, OpenGL flavour. See dual_textured3d.frag.glsl for the Vulkan
// flavour and the formula this mirrors.

#version 450 core

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

layout(binding = 2) uniform sampler2D colorMap;
layout(binding = 4) uniform sampler2D colorMap2;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 base = texture(colorMap, vTexCoord);
    vec4 overlay = texture(colorMap2, vTexCoord);
    base.rgb *= 2.0;
    vec4 color = base * overlay * vColor;

    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
