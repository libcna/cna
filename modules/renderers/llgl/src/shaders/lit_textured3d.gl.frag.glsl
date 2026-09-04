// SPDX-License-Identifier: MS-PL
// Lit, textured 3D fragment shader, OpenGL flavour. Shared by both lit vertex shaders
// (with and without a vertex colour) -- their outputs are identical, only the tint they compute
// differs -- and applies the alpha test and fog through the exact same code every other 3D
// fragment shader in this directory uses, so the three cannot drift apart.
//
// The lighting equation matches this project's own established convention (see e.g.
// SdlGpu's lit_textured3d.frag.glsl): ambient plus per-light Lambertian diffuse, gated so a light
// facing away from the surface contributes neither diffuse nor specular, half-vector Blinn-Phong
// specular applied once per light and scaled by the material's SpecularColor/SpecularPower, the
// lit sum multiplied by the material tint, EmissiveColor added afterward (not scaled by it), then
// the texture multiplied in, and finally specular added back scaled by the resulting alpha.

#version 450 core

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

layout(binding = 2) uniform sampler2D colorMap;

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec4 vTint;
layout(location = 4) in float vFogFactor;

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
    vec4 tex = texture(colorMap, vTexCoord);

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
    vec4 color = vec4(lit, vTint.a) * tex;
    color.rgb += specularRGB * color.a;

    float alphaTestResult = ((alphaTest.y > 0.0)
        ? ((abs(color.a - alphaTest.x) < alphaTest.y) ? alphaTest.z : alphaTest.w)
        : ((color.a < alphaTest.x) ? alphaTest.z : alphaTest.w));
    if (alphaTestResult < 0.0)
        discard;

    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
