// SPDX-License-Identifier: MS-PL
// SpriteBatch vertex shader, OpenGL flavour. Behaviourally identical to sprite2d.vert.glsl; the
// differences are forced by desktop GLSL, which has no separate texture/sampler objects (see the
// fragment shader) and spells its resource bindings differently.
//
// Every binding, attribute and varying is given an explicit location or binding number rather than
// left to be matched by name. Name matching is what the OpenGL module does by default, and on this
// project's own test environment (Mesa llvmpipe, GL 4.5 core) it did not work: only the first
// vertex attribute arrived, and the uniform block was never fed, so the whole sprite collapsed to
// a degenerate triangle. See docs/llgl-renderer.md and known_bugs.md -- the OpenGL module is still
// an unverified path, and these qualifiers narrow rather than close that gap.

#version 450 core

layout(std140, binding = 1) uniform Scene
{
    mat4 mvpMatrix;
};

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;

void main()
{
    gl_Position = mvpMatrix * vec4(position, 0.0, 1.0);
    vTexCoord   = texCoord;
    vColor      = color;
}
