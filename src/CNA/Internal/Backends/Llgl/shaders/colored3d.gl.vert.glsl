// SPDX-License-Identifier: MS-PL
// Colour-only 3D vertex shader, OpenGL flavour. Behaviourally identical to colored3d.vert.glsl.
// Every binding and location is explicit for the same reason as in the sprite shaders: name-based
// matching is what the OpenGL module does by default, and being explicit removes a whole class of
// silent mismatch.

#version 450 core

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vColor      = color;
}
