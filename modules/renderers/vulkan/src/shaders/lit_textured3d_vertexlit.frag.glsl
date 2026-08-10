#version 450

// Task 1103: per-vertex-lit sibling of lit_textured3d.frag.glsl — see the vertex shader's own
// header comment. Non-lighting math (texture sample, fog) is structurally identical to
// lit_textured3d.frag.glsl; only the SOURCE of the lit/specular RGB differs (interpolated
// vertex-stage varyings instead of a per-fragment recompute).

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragFogFactor;
layout(location = 2) in vec3  fragLitRGB;
layout(location = 3) in vec3  fragSpecularRGB;
layout(location = 4) in float fragAlpha;

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

// Same UBO as the vertex shader (set=0, binding=1) — only fogColorEnabled.xyz is read here.
layout(set = 0, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;
    vec4 eyePos_pad;
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;
} lp;

void main() {
    vec4 tex = (pc.textureEnabled > 0.5) ? texture(uTexture, fragUV) : vec4(1.0);
    vec4 color = vec4(fragLitRGB, fragAlpha) * tex;
    // Specular is added after the texture*diffuse multiply, scaled by the resulting alpha
    // (FNA's AddSpecular macro), never by the texture directly — matches lit_textured3d.frag.glsl.
    color.rgb += fragSpecularRGB * color.a;
    color.rgb = mix(lp.fogColorEnabled.xyz, color.rgb, fragFogFactor);
    outColor = color;
}
