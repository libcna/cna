#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in float fragFogFactor;
// plans/plan_vulkan.md VULKAN-150: the second sampler's own coordinate set.
layout(location = 3) in vec2 fragUV1;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(set = 0, binding = 1) uniform sampler2D uTexture2;

layout(push_constant) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

// Task 899: fog UBO at binding=2 (bindings 0/1 are the two texture samplers).
layout(set = 0, binding = 2) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    vec4 tex1 = texture(uTexture,  fragUV);
    vec4 tex2 = texture(uTexture2, fragUV1);   // VULKAN-150: TEXCOORD1, not TEXCOORD0
    tex1.rgb *= 2.0;
    outColor  = tex1 * tex2 * fragTint;
    // Task 899: mix toward FogColor as fragFogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, fragFogFactor);
}
