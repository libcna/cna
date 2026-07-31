// SPDX-License-Identifier: MS-PL
// Reference copy of the uniform block every 3D shader in this directory shares. It is NOT included
// by the shaders (glslangValidator would need an include path, and the OpenGL flavours are embedded
// as plain source), so it exists to keep one authoritative description of the layout next to the
// shaders that must all agree with it, and with LlglGraphicsBackend::FillEffectUniforms.
//
// std140, 128 bytes:
//   offset   0  mat4  mvpMatrix     world * view * projection, already transposed for GLSL
//   offset  64  vec4  diffuseColor  rgb = material diffuse, a = alpha
//   offset  80  vec4  fogColor      rgb = fog colour, a = 1 when fog is enabled
//   offset  96  vec4  fogVector     dot with the object-space position gives the fog factor
//   offset 112  vec4  alphaTest     x = reference, y = tolerance, z = pass weight, w = fail weight
