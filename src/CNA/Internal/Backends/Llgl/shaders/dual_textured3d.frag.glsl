// SPDX-License-Identifier: MS-PL
// DualTextureEffect fragment shader, Vulkan flavour (SPIR-V).
//
// Reuses the plain textured/colored_textured vertex shader as-is (identical vertex-side
// behaviour -- transform, tint, fog factor, UV passthrough): only this fragment shader and its
// pipeline layout differ from the single-texture path, because DualTextureEffect samples a SECOND
// texture through the SAME texture coordinate (no separate UV set), matching FNA's own
// PSDualTexture: `base = SAMPLE(Texture, uv); base.rgb *= 2; color = base * overlay * tint`, where
// `overlay = SAMPLE(Texture2, uv)`. There is no alpha test for this effect, matching FNA.
//
// The uniform block is the one described in effect3d_common.glsl.inc; every 3D shader here shares
// it byte for byte, so a change to one must change all of them and FillEffectUniforms together.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    // Unused here, but must stay present so this shader's Transform block byte-matches every
    // other unlit 3D shader it can be linked with in the same OpenGL program (GL requires an
    // identically named/laid-out uniform block to match exactly across linked stages) -- see
    // colored3d.vert.glsl for the field this backs.
    vec4 vertexColorEnabledPad;
};

layout(binding = 2) uniform texture2D colorMap;
layout(binding = 3) uniform sampler samplerState;
layout(binding = 4) uniform texture2D colorMap2;
layout(binding = 5) uniform sampler samplerState2;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 base = texture(sampler2D(colorMap, samplerState), vTexCoord);
    vec4 overlay = texture(sampler2D(colorMap2, samplerState2), vTexCoord);
    base.rgb *= 2.0;
    vec4 color = base * overlay * vColor;

    // Fog blends towards the fog colour without touching alpha, matching the stock effects.
    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
