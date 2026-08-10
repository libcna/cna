// SPDX-License-Identifier: MS-PL
// SkinnedEffect fragment shader, Vulkan flavour (SPIR-V). See skinned3d.vert.glsl for the
// SkinnedParams/BoneBlock uniform blocks this shares.
//
// The lighting equation is BasicEffect's own (see lit_textured3d.frag.glsl): per-light Lambertian
// diffuse gated so a light facing away contributes neither diffuse nor specular, half-vector
// Blinn-Phong specular scaled by the material's SpecularColor/SpecularPower -- EXCEPT there is no
// separate ambient term here, because SkinnedEffect::FillGpuDrawParams pre-folds
// (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha into emissiveColorPad.xyz already
// (matching EnvironmentMapEffect's own convention, not BasicEffect's separate-ambient one). Always
// textured -- real XNA's SkinnedEffect has no untextured variant, unlike BasicEffect.

#version 450

layout(std140, binding = 1) uniform SkinnedParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 emissiveColorPad;
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light0SpecularPad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light1SpecularPad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 light2SpecularPad;
    vec4 eyePositionWorldWeights;
    vec4 specularColorPower;
    vec4 fogColor;
    vec4 fogVector;
};

layout(std140, binding = 2) uniform BoneBlock
{
    mat4 bones[72];
};

layout(binding = 3) uniform texture2D colorMap;
layout(binding = 4) uniform sampler samplerState;

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

// A disabled/never-configured DirectionalLight can forward Direction=(0,0,0) (matches FNA's own
// zeroing) -- normalize() on a true zero vector is undefined and can poison the whole light sum.
vec3 safeNormalize(vec3 v)
{
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 E = safeNormalize(eyePositionWorldWeights.xyz - vWorldPos);

    vec3 nL0 = safeNormalize(light0DirPad.xyz);
    vec3 nL1 = safeNormalize(light1DirPad.xyz);
    vec3 nL2 = safeNormalize(light2DirPad.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float ndl0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float ndl1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float ndl2 = max(dotL2, 0.0);

    vec3 lightSum = ndl0 * light0DiffusePad.xyz
                  + ndl1 * light1DiffusePad.xyz
                  + ndl2 * light2DiffusePad.xyz;
    vec3 litRGB = lightSum * diffuseColor.rgb + emissiveColorPad.xyz;

    vec3 h0 = safeNormalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularColorPower.w);
    vec3 h1 = safeNormalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularColorPower.w);
    vec3 h2 = safeNormalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularColorPower.w);
    vec3 specularRGB = (spec0 * light0SpecularPad.xyz + spec1 * light1SpecularPad.xyz
                        + spec2 * light2SpecularPad.xyz) * specularColorPower.xyz;

    vec4 tex = texture(sampler2D(colorMap, samplerState), vTexCoord);
    vec4 color = vec4(litRGB * tex.rgb, diffuseColor.a * tex.a);
    color.rgb += specularRGB * color.a;

    // vFogFactor is "how much fog" (0 = none, 1 = full), matching this renderer's own convention
    // (see lit_textured3d.frag.glsl / env_map3d.frag.glsl) -- NOT the plain Vulkan renderer's
    // opposite convention.
    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
