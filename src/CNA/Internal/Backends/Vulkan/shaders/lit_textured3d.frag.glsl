#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec4  fragTint;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

// Task 897: DirectionalLight1/DirectionalLight2 + EmissiveColor, forwarded via a small UBO
// since the 128-byte push constant below is already fully packed. Not needed by the vertex
// shader (lighting math is all per-pixel here), so fragment-stage only.
layout(set = 0, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
} lp;

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

void main() {
    vec4 tex = (pc.textureEnabled > 0.5) ? texture(uTexture, fragUV) : vec4(1.0);

    vec4 color;
    if (pc.lightingEnabled > 0.5) {
        vec3 N  = normalize(fragNormal);
        // Direction fields point FROM the light, so negate for dot with N.
        float NdotL0 = max(dot(N, -normalize(pc.light0Dir)), 0.0);
        float NdotL1 = max(dot(N, -normalize(lp.light1Dir_pad.xyz)), 0.0);
        float NdotL2 = max(dot(N, -normalize(lp.light2Dir_pad.xyz)), 0.0);
        vec3 lightSum = pc.ambientColor + NdotL0 * pc.light0Diffuse
                        + NdotL1 * lp.light1Diffuse_pad.xyz + NdotL2 * lp.light2Diffuse_pad.xyz;
        // EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it
        // (matches FNA's Lighting.fxh: result.Diffuse = sum*DiffuseColor + EmissiveColor).
        vec3 lit = lightSum * fragTint.rgb + lp.emissiveColor_pad.xyz;
        color = vec4(lit, fragTint.a) * tex;
    } else {
        color = fragTint * tex;
    }
    outColor = color;
}
