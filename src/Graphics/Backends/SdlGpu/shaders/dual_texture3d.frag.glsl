#version 450

// Shared by dual_texture3d.vert.glsl (stride 20) and dual_texture_colored3d.vert.glsl (stride 24).
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in vec4 fragFog;    // REMED-GFX-009
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 2, binding = 1) uniform sampler2D uTexture2;

void main() {
    vec4 tex1 = texture(uTexture, fragUV);
    vec4 tex2 = texture(uTexture2, fragUV);
    tex1.rgb *= 2.0;
    outColor = tex1 * tex2 * fragTint;
    // REMED-GFX-009: blend toward FogColor (RGB only). fragFog.a = keep (1 no fog, 0 full fog).
    outColor.rgb = mix(fragFog.rgb, outColor.rgb, fragFog.a);
}
