#version 300 es
precision highp float;

layout(location = 0) out vec4 cnaOitAccumulation;
layout(location = 1) out vec4 cnaOitRevealage;
uniform vec4 uEffectParams;
uniform float uScalar;

void main()
{
    float alpha = uEffectParams.a;
    float z = clamp(uScalar, 0.0, 1.0);
    float weight = alpha * clamp(0.03 / (1e-5 + pow(z, 4.0)), 1e-2, 3e3);
    cnaOitAccumulation = vec4(uEffectParams.rgb * alpha * weight, alpha * weight);
    cnaOitRevealage = vec4(log(max(1.0 - alpha, 1e-4)), 0.0, 0.0, 0.0);
}
