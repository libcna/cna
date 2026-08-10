// SPDX-License-Identifier: MS-PL
// Lit, coloured and textured 3D vertex shader, Vulkan flavour (SPIR-V). A vertex colour is
// multiplied into the tint as well; see lit_textured3d.vert.glsl for the plain variant without
// one -- no standard XNA vertex type combines Color and Normal, but nothing stops a game from
// declaring one, and this renderer's shader-variant selection follows the vertex layout either way.
//
// The uniform block extends the common one (see effect3d_common.glsl.inc) with everything a lit
// draw needs -- the world matrix (for the normal and world-position transforms) and all three
// directional lights -- appended AFTER the fields every 3D shader shares, so the unlit shaders'
// existing byte layout is untouched by this file's existence.
//
// Lighting is evaluated per pixel, not per vertex -- the same choice every other established CNA
// renderer except D3D9 makes (see GpuDrawParams::preferPerPixelLighting's own doc comment); this
// renderer ignores that field identically.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    mat4 worldMatrix;
    // .w > 0.5 when the effect's VertexColorEnabled is true (reuses this vec4's otherwise-unused
    // fourth component -- ambientColorLighting itself is an RGB colour); otherwise the colour
    // attribute below must NOT multiply into the tint, even though the vertex layout carries one.
    vec4 ambientColorLighting;
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light0SpecularPad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light1SpecularPad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 light2SpecularPad;
    vec4 emissiveColorPad;
    vec4 eyePositionWorldPad;
    vec4 specularColorPower;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 normal;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec4 vTint;
layout(location = 4) out float vFogFactor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    vTexCoord   = texCoord;

    // Normals are transformed by the inverse-transpose of the world matrix so a non-uniform scale
    // does not skew them; GLSL's built-in inverse() makes this a one-liner on every module.
    mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));
    vNormal     = normalize(normalMatrix * normal);
    vWorldPos   = (worldMatrix * vec4(position, 1.0)).xyz;
    vTint       = (ambientColorLighting.w > 0.5) ? diffuseColor * color : diffuseColor;
    vFogFactor  = clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0) * fogColor.a;
}
