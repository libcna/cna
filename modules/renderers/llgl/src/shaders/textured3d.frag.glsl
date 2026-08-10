// SPDX-License-Identifier: MS-PL
// Textured 3D fragment shader, Vulkan flavour (SPIR-V).
//
// The uniform block is the one described in effect3d_common.glsl.inc; every 3D shader here shares
// it byte for byte, so a change to one must change all of them and FillEffectUniforms together.
// Attribute locations follow the renderer's usage-to-location mapping (position 0, colour 1,
// texture coordinate 2), which is why a layout without vertex colours simply has no location 1.

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

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 color = texture(sampler2D(colorMap, samplerState), vTexCoord) * vColor;

    // XNA's alpha test: pass when |a - ref| < tolerance for the equal/not-equal comparisons, or
    // when a < ref otherwise; the two weights encode which side passes. A negative result discards.
    float alphaTestResult = ((alphaTest.y > 0.0)
        ? ((abs(color.a - alphaTest.x) < alphaTest.y) ? alphaTest.z : alphaTest.w)
        : ((color.a < alphaTest.x) ? alphaTest.z : alphaTest.w));
    if (alphaTestResult < 0.0)
        discard;

    // Fog blends towards the fog colour without touching alpha, matching the stock effects.
    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
