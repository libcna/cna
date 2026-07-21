#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in float fragFogFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

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

// Task 899: fog forwarded via the shared colored3d/textured3d/colored_textured3d bundle's
// dynamic UBO (set=0, binding=1) -- the 128-byte push constant above has zero spare bytes.
layout(set = 0, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
} fog;

void main() {
    vec4 tex = (pc.textureEnabled > 0.5) ? texture(uTexture, fragUV) : vec4(1.0);
    outColor = tex * fragTint;
    // Task 899: mix toward FogColor as fragFogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = mix(fog.fogColorEnabled.xyz, outColor.rgb, fragFogFactor);
}
