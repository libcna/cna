// SPDX-License-Identifier: MS-PL
// Colour-only 3D fragment shader, Vulkan flavour (SPIR-V). See colored3d.gl.frag.glsl for the
// OpenGL flavour.

#version 450

layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
