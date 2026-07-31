// SPDX-License-Identifier: MS-PL
// Colour-only 3D vertex shader, Vulkan flavour (SPIR-V). The OpenGL flavour is
// colored3d.gl.vert.glsl -- behaviourally identical, different resource-declaration syntax only.
//
// This is the shader behind IGraphicsBackend::DrawColoredPrimitives, i.e. the equivalent of
// BasicEffect with VertexColorEnabled and nothing else: no texture, no lighting, no fog.
//
// The matrix arrives already combined (world * view * projection) and already transposed into the
// column-vector form GLSL multiplies with, so there is no convention juggling here. When the
// render system clips depth to [-1,+1] rather than [0,+1], the backend folds that correction into
// the same matrix, so this shader is identical on both.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vColor      = color;
}
