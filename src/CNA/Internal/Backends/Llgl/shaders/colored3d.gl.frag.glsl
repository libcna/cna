// SPDX-License-Identifier: MS-PL
// Colour-only 3D fragment shader, OpenGL flavour. See colored3d.frag.glsl for the Vulkan flavour.

#version 450 core

layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
