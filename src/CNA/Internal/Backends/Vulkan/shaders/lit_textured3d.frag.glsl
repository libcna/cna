#version 450

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec4  fragTint;
layout(location = 3) in vec3  fragWorldPos;
layout(location = 4) in float fragFogFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

// Task 897/886/898: DirectionalLight1/DirectionalLight2 + EmissiveColor + specular, forwarded
// via a small UBO since the 128-byte push constant below is already fully packed.
layout(set = 0, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;                  // vertex-stage only; unused here
    vec4 eyePos_pad;
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower;     // xyz = material SpecularColor, w = SpecularPower
    // Task 888: fog, packed into the UBO's previously-unused trailing 32 bytes. Vertex-stage
    // computes fragFogFactor from fogColorEnabled.w/fogStartEnd; only fogColorEnabled.xyz needed here.
    vec4 fogColorEnabled;        // xyz = FogColor, w = fogEnabled
    vec4 fogStartEnd;            // x = fogStart, y = fogEnd, zw = unused
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
        vec3 N = normalize(fragNormal);
        vec3 E = normalize(lp.eyePos_pad.xyz - fragWorldPos);
        vec3 nL0 = normalize(pc.light0Dir);
        vec3 nL1 = normalize(lp.light1Dir_pad.xyz);
        vec3 nL2 = normalize(lp.light2Dir_pad.xyz);
        // Direction fields point FROM the light, so negate for dot with N.
        float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
        float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
        float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
        vec3 lightSum = pc.ambientColor + NdotL0 * pc.light0Diffuse
                        + NdotL1 * lp.light1Diffuse_pad.xyz + NdotL2 * lp.light2Diffuse_pad.xyz;
        // Half-vector Blinn-Phong specular (FNA's Lighting.fxh ComputeLights), gated by the same
        // zeroL "does this light face the surface" term used for diffuse. Material SpecularColor
        // is applied once to the summed per-light contribution, not per-light.
        vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, lp.specularColorPower.w);
        vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, lp.specularColorPower.w);
        vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, lp.specularColorPower.w);
        vec3 specularRGB = (spec0 * lp.light0Specular_pad.xyz + spec1 * lp.light1Specular_pad.xyz
                            + spec2 * lp.light2Specular_pad.xyz) * lp.specularColorPower.xyz;
        // EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it
        // (matches FNA's Lighting.fxh: result.Diffuse = sum*DiffuseColor + EmissiveColor).
        vec3 lit = lightSum * fragTint.rgb + lp.emissiveColor_pad.xyz;
        color = vec4(lit, fragTint.a) * tex;
        // Specular is added after the texture*diffuse multiply, scaled by the resulting alpha
        // (FNA's AddSpecular macro), never by the texture directly.
        color.rgb += specularRGB * color.a;
    } else {
        color = fragTint * tex;
    }
    // Task 888: mix toward FogColor as fragFogFactor -> 0 (matches EasyGL's established formula).
    color.rgb = mix(lp.fogColorEnabled.xyz, color.rgb, fragFogFactor);
    outColor = color;
}
