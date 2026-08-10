// SPDX-License-Identifier: MS-PL
// EnvironmentMapEffect vertex shader, Vulkan flavour (SPIR-V).
//
// Unlike every other 3D shader in this directory, this one does NOT share the common
// `Transform` uniform block (see effect3d_common.glsl.inc) -- EnvironmentMapEffect needs a
// genuinely different field set (Fresnel factor, environment map amount/specular, no per-light
// specular or alpha test) and this vertex/fragment pair is never linked with any other shader in
// this renderer, so there is no cross-program layout to keep byte-compatible with. The layout
// mirrors the Vulkan renderer's own EnvMapParams uniform block (env_map3d.vert.glsl there), with
// the MVP/world matrices folded into the same buffer instead of a separate push constant, since
// this renderer's own established convention (see lit_textured3d.vert.glsl) is one constant
// buffer per draw, not push constants.

#version 450

layout(std140, binding = 1) uniform EnvMapParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 emissiveColorAmount;         // xyz = pre-folded emissive+ambient*diffuse*alpha, w = EnvironmentMapAmount
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 eyePositionWorldPad;
    vec4 envMapSpecularFresnelFactor; // xyz = EnvironmentMapSpecular, w = FresnelFactor
    vec4 fresnelEnabledPad;           // x = FresnelEnabled (0/1)
    vec4 fogColor;                    // xyz = FogColor, w = fogEnabled (0/1)
    vec4 fogVector;
};

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 normal;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vTexCoord   = texCoord;

    // Normals are transformed by the inverse-transpose of the world matrix so a non-uniform scale
    // does not skew them, matching every other lit shader here.
    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal     = normalize(normalMatrix * normal);
    vWorldPos   = (worldMatrix * vec4(position, 1.0)).xyz;
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
