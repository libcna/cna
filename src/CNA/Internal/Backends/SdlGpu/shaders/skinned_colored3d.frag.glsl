#version 450

// Identical to lit_textured3d.frag.glsl's Blinn-Phong math, plus one addition: a per-vertex
// Color multiplied into the FINAL combined diffuse+specular output, gated by
// uVertexColorEnabled. The multiply happens AFTER specular is added (not folded into fragTint,
// which only feeds the diffuse term) -- doing it earlier would leave the specular term
// unscaled by vertex color, a real bug EasyGLGraphicsBackend's own vertex-color skinned shaders
// hit once and fixed by moving the multiply here (see that backend's EnsureSkinnedProgram()/
// EnsureSkinnedVertexLitProgram() doc comments).

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec3  fragNormal;
layout(location = 2) in vec4  fragTint;
layout(location = 3) in vec3  fragWorldPos;
layout(location = 4) in vec4  fragVertexColor;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;

layout(set = 3, binding = 0) uniform PC {
    mat4  mvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 3, binding = 1) uniform LitLightParams {
    vec4 light1Dir_pad;
    vec4 light1Diffuse_pad;
    vec4 light2Dir_pad;
    vec4 light2Diffuse_pad;
    vec4 emissiveColor_pad;
    mat4 world;             // vertex-stage only; unused here
    vec4 eyePos_pad;
    vec4 light0Specular_pad;
    vec4 light1Specular_pad;
    vec4 light2Specular_pad;
    vec4 specularColorPower; // xyz = material SpecularColor, w = SpecularPower
} lp;

vec3 safeNormalize(vec3 v) {
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

void main() {
    vec4 tex = (pc.textureEnabled > 0.5) ? texture(uTexture, fragUV) : vec4(1.0);

    vec4 color;
    if (pc.lightingEnabled > 0.5) {
        vec3 N = normalize(fragNormal);
        vec3 E = safeNormalize(lp.eyePos_pad.xyz - fragWorldPos);
        vec3 nL0 = safeNormalize(pc.light0Dir);
        vec3 nL1 = safeNormalize(lp.light1Dir_pad.xyz);
        vec3 nL2 = safeNormalize(lp.light2Dir_pad.xyz);
        float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
        float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
        float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
        vec3 lightSum = pc.ambientColor + NdotL0 * pc.light0Diffuse
                        + NdotL1 * lp.light1Diffuse_pad.xyz + NdotL2 * lp.light2Diffuse_pad.xyz;
        vec3 h0 = safeNormalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, lp.specularColorPower.w);
        vec3 h1 = safeNormalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, lp.specularColorPower.w);
        vec3 h2 = safeNormalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, lp.specularColorPower.w);
        vec3 specularRGB = (spec0 * lp.light0Specular_pad.xyz + spec1 * lp.light1Specular_pad.xyz
                            + spec2 * lp.light2Specular_pad.xyz) * lp.specularColorPower.xyz;
        vec3 lit = lightSum * fragTint.rgb + lp.emissiveColor_pad.xyz;
        color = vec4(lit, fragTint.a) * tex;
        color.rgb += specularRGB * color.a;
    } else {
        color = fragTint * tex;
    }

    vec4 vertexColor = (pc.vertexColorEnabled > 0.5) ? fragVertexColor : vec4(1.0);
    color *= vertexColor;
    outColor = color;
}
