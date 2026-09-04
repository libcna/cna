// SPDX-License-Identifier: MS-PL
// SpriteBatch vertex shader, Vulkan flavour (SPIR-V). The OpenGL flavour of the same shader is
// sprite2d.gl.vert.glsl -- they must stay behaviourally identical; only the resource-declaration
// syntax differs (Vulkan needs explicit binding numbers and separate texture/sampler objects,
// desktop GLSL has neither).
//
// Positions arrive in the renderer's logical (virtual-resolution) pixel space with the origin in
// the top-left, exactly as SpriteBatch produces them. The projection in the Scene block does the
// whole pixel-space -> clip-space mapping, including the Y flip, so there is no hand-rolled NDC
// arithmetic here and no per-renderer sign correction: LlglRenderer builds the matrix from
// the render system's own reported screen origin.

#version 450

layout(std140, binding = 1) uniform Scene
{
    mat4 mvpMatrix;
};

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 0.0, 1.0);
    vTexCoord   = texCoord;
    vColor      = color;
}
