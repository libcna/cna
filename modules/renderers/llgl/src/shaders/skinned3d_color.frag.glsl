// SPDX-License-Identifier: MS-PL
// SkinnedEffect.VertexColorEnabled fragment shader, Vulkan flavour (SPIR-V). LLGL-37. See
// skinned3d_color.vert.glsl for why this is a separate shader from skinned3d.frag.glsl, and
// skinned3d.frag.glsl for the lighting equation this mirrors -- unchanged here except for the
// vertex-colour modulation added at the end.
//
// Gate and modulation order match every other renderer's own SkinnedEffect.VertexColorEnabled
// implementation exactly (EasyGL's EnsureSkinnedProgram() fragment shader is the reference): vertex
// colour's alpha multiplies into the combined colour BEFORE the specular highlight is added (so
// alpha itself is never double-counted against specular), and its RGB multiplies the WHOLE combined
// diffuse+specular output AFTER the specular add (so VertexColorEnabled=true with a pure black
// vertex colour genuinely zeroes the pixel -- an unmodulated specular highlight added afterward
// would otherwise leak through).

#version 450

layout(std140, binding = 1) uniform SkinnedParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 emissiveColorPad; // .w > 0.5 when VertexColorEnabled is true -- see the vertex shader's own doc comment.
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
layout(location = 4) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

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
    vec4 vc = (emissiveColorPad.w > 0.5) ? vColor : vec4(1.0);
    vec4 color = vec4(litRGB * tex.rgb, diffuseColor.a * tex.a * vc.a);
    color.rgb += specularRGB * color.a;
    color.rgb *= vc.rgb;

    color.rgb = mix(color.rgb, fogColor.rgb, vFogFactor);

    fragColor = color;
}
