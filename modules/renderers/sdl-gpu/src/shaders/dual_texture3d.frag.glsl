#version 450

// Shared by dual_texture3d.vert.glsl (stride 20) and dual_texture_colored3d.vert.glsl (stride 24).
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec2 fragUV1;
layout(location = 2) in vec4 fragTint;
layout(location = 3) in vec4 fragFog;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 2, binding = 1) uniform sampler2D uTexture2;

layout(set = 3, binding = 0) uniform SamplerLodBias {
    vec4 slots0To3;
    vec4 slots4To7;
} samplerLodBias;

void main() {
    vec4 tex1 = texture(uTexture, fragUV, samplerLodBias.slots0To3.x);
    vec4 tex2 = texture(uTexture2, fragUV1, samplerLodBias.slots0To3.y);
    tex1.rgb *= 2.0;
    outColor = tex1 * tex2 * fragTint;
    // Premultiplied fog uses the completed fragment alpha; fragFog.a is the keep factor.
    outColor.rgb = mix(fragFog.rgb * outColor.a, outColor.rgb, fragFog.a);
}
