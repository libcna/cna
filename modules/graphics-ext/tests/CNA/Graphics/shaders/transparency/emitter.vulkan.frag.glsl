#version 450

layout(location = 0) out vec4 cnaOitAccumulation;
layout(location = 1) out vec4 cnaOitRevealage;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uEffectParams;
    float uScalar;
} pc;

void main()
{
    float alpha = pc.uEffectParams.a;
    float z = clamp(pc.uScalar, 0.0, 1.0);
    float weight = alpha * clamp(0.03 / (1e-5 + pow(z, 4.0)), 1e-2, 3e3);
    cnaOitAccumulation = vec4(pc.uEffectParams.rgb * alpha * weight, alpha * weight);
    cnaOitRevealage = vec4(log(max(1.0 - alpha, 1e-4)), 0.0, 0.0, 0.0);
}
