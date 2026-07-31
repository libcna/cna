// SPDX-License-Identifier: MS-PL
// Untextured 3D fragment shader, Vulkan flavour (SPIR-V). Shares the alpha-test and fog
// handling of its textured sibling so the two cannot drift apart.
//
// The uniform block is the one described in effect3d_common.glsl.inc; every 3D shader here shares
// it byte for byte, so a change to one must change all of them and FillEffectUniforms together.
// Attribute locations follow the backend's usage-to-location mapping (position 0, colour 1,
// texture coordinate 2), which is why a layout without vertex colours simply has no location 1.

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
};


layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec4 color = vColor;

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
