// SPDX-License-Identifier: MS-PL
// EnvironmentMapEffect fragment shader, OpenGL flavour. See env_map3d.frag.glsl for the Vulkan
// flavour and the formula this mirrors (including the three formula pitfalls documented there).

#version 450 core

layout(std140, binding = 1) uniform EnvMapParams
{
    mat4 mvpMatrix;
    mat4 worldMatrix;
    vec4 diffuseColor;
    vec4 emissiveColorAmount;
    vec4 light0DirPad;
    vec4 light0DiffusePad;
    vec4 light1DirPad;
    vec4 light1DiffusePad;
    vec4 light2DirPad;
    vec4 light2DiffusePad;
    vec4 eyePositionWorldPad;
    vec4 envMapSpecularFresnelFactor;
    vec4 fresnelEnabledPad;
    vec4 fogColor;
    vec4 fogVector;
};

layout(binding = 2) uniform sampler2D colorMap;
layout(binding = 4) uniform samplerCube envMap;

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in float vFogFactor;

layout(location = 0) out vec4 fragColor;

vec3 safeNormalize(vec3 v)
{
    float len2 = dot(v, v);
    return len2 > 0.0 ? v * inversesqrt(len2) : vec3(0.0);
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 E = safeNormalize(eyePositionWorldPad.xyz - vWorldPos);

    vec3 nL0 = safeNormalize(light0DirPad.xyz);
    vec3 nL1 = safeNormalize(light1DirPad.xyz);
    vec3 nL2 = safeNormalize(light2DirPad.xyz);
    float ndl0 = max(dot(N, -nL0), 0.0);
    float ndl1 = max(dot(N, -nL1), 0.0);
    float ndl2 = max(dot(N, -nL2), 0.0);

    vec3 lightSum = ndl0 * light0DiffusePad.xyz + ndl1 * light1DiffusePad.xyz + ndl2 * light2DiffusePad.xyz;
    vec3 litRGB = lightSum * diffuseColor.rgb + emissiveColorAmount.xyz;

    vec4 texColor  = texture(colorMap, vTexCoord);
    vec3 reflDir   = reflect(-E, N);
    vec4 envSample = texture(envMap, reflDir);

    vec3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = diffuseColor.a * texColor.a;
    float viewAngle = dot(E, N);
    float blendFactor = (fresnelEnabledPad.x > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), envMapSpecularFresnelFactor.w) * emissiveColorAmount.w
        : emissiveColorAmount.w;

    vec3 rgb = mix(baseColor, envSample.rgb * combinedAlpha, blendFactor)
             + envMapSpecularFresnelFactor.xyz * envSample.a * combinedAlpha;
    // vFogFactor is "how much fog" (0 = none, 1 = full), matching this renderer's own convention
    // (see lit_textured3d.gl.frag.glsl) -- NOT the plain Vulkan renderer's opposite "how much of
    // the original colour to keep" convention its own env_map3d.vert.glsl computes it as.
    rgb = mix(rgb, fogColor.rgb, vFogFactor);

    fragColor = vec4(rgb, combinedAlpha);
}
