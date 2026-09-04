// SPDX-License-Identifier: MS-PL
// Lit, UNTEXTURED 3D fragment shader, Vulkan flavour (SPIR-V) (LLGL-31). Identical to
// lit_textured3d.frag.glsl minus the texture sample -- see that file for the lighting equation
// and why alpha test/fog are applied through the exact same code every 3D fragment shader here
// uses. Input locations match lit_colored3d.vert.glsl's outputs (no vTexCoord).

#version 450

layout(std140, binding = 1) uniform Transform
{
    mat4 mvpMatrix;
    vec4 diffuseColor;
    vec4 fogColor;
    vec4 fogVector;
    vec4 alphaTest;
    mat4 worldMatrix;
    vec4 ambientColorLighting;
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light0SpecularPad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light1SpecularPad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 light2SpecularPad;
    vec4 emissiveColorPad;
    vec4 eyePositionWorldPad;
    vec4 specularColorPower;
};

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec4 vTint;
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
    vec3 E = safeNormalize(eyePositionWorldPad.xyz - vWorldPos);

    // Direction fields point FROM the light, so negate for the dot-with-normal.
    vec3 nL0 = safeNormalize(light0DirPad.xyz);
    vec3 nL1 = safeNormalize(light1DirPad.xyz);
    vec3 nL2 = safeNormalize(light2DirPad.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float ndl0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float ndl1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float ndl2 = max(dotL2, 0.0);

    vec3 lightSum = ambientColorLighting.xyz
                   + ndl0 * light0DiffusePad.xyz
                   + ndl1 * light1DiffusePad.xyz
                   + ndl2 * light2DiffusePad.xyz;

    vec3 h0 = safeNormalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularColorPower.w);
    vec3 h1 = safeNormalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularColorPower.w);
    vec3 h2 = safeNormalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularColorPower.w);
    vec3 specularRGB = (spec0 * light0SpecularPad.xyz + spec1 * light1SpecularPad.xyz
                        + spec2 * light2SpecularPad.xyz) * specularColorPower.xyz;

    vec3 lit = lightSum * vTint.rgb + emissiveColorPad.xyz;
    vec4 color = vec4(lit, vTint.a);
    color.rgb += specularRGB * color.a;

    float alphaTestResult = ((alphaTest.y > 0.0)
        ? ((abs(color.a - alphaTest.x) < alphaTest.y) ? alphaTest.z : alphaTest.w)
        : ((color.a < alphaTest.x) ? alphaTest.z : alphaTest.w));
    if (alphaTestResult < 0.0)
        discard;

    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
