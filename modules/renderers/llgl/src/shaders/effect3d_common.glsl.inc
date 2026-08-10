// SPDX-License-Identifier: MS-PL
// Reference copy of the uniform block every 3D shader in this directory shares. It is NOT included
// by the shaders (glslangValidator would need an include path, and the OpenGL flavours are embedded
// as plain source), so it exists to keep one authoritative description of the layout next to the
// shaders that must all agree with it, and with LlglRenderer::FillEffectUniforms.
//
// std140, 144 bytes:
//   offset   0  mat4  mvpMatrix             world * view * projection, already transposed for GLSL
//   offset  64  vec4  diffuseColor          rgb = material diffuse, a = alpha
//   offset  80  vec4  fogColor              rgb = fog colour, a = 1 when fog is enabled
//   offset  96  vec4  fogVector             dot with the object-space position gives the fog factor
//   offset 112  vec4  alphaTest             x = reference, y = tolerance, z = pass weight, w = fail weight
//   offset 128  vec4  vertexColorEnabledPad x > 0.5 when the effect's VertexColorEnabled is true;
//                                           only read by the vertex shaders that carry a colour
//                                           attribute (colored3d/colored_textured3d), but every
//                                           unlit 3D shader (including the ones that never read it)
//                                           must still declare it, since GLSL/OpenGL requires an
//                                           identically named/laid-out uniform block across every
//                                           stage linked into one program. The lit shaders instead
//                                           reuse ambientColorLighting.w below for the same flag,
//                                           since offset 128 becomes worldMatrix there.
