#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in float fragFogFactor;
layout(location = 0) out vec4 outColor;

// Task 899: this shader never samples a texture, but binding=0 is still declared in the shared
// colored3d/textured3d/colored_textured3d descriptor-set-layout (a fallback white texture is
// bound there for colored3d draws) so all three pipelines can share one bundle. Only binding=1
// (fog) is actually read here.
layout(set = 0, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
} fog;

void main() {
    outColor = fragColor;
    // Task 899: mix toward FogColor as fragFogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, fragFogFactor);
}
