#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec4 fragFog;    // REMED-GFX-009
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
    // REMED-GFX-009: blend toward FogColor (RGB only). fragFog.a = keep (1 no fog, 0 full fog).
    outColor.rgb = mix(fragFog.rgb, outColor.rgb, fragFog.a);
}
