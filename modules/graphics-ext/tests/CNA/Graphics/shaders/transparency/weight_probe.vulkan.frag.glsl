#version 450

layout(location = 0) in float PositionX;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uEffectParams;
} pc;

void main()
{
    float depth = mix(pc.uEffectParams.y, pc.uEffectParams.z, PositionX);
    float z = clamp(depth / max(pc.uEffectParams.w, 1e-4), 0.0, 1.0);
    float weight = pc.uEffectParams.x * clamp(0.03 / (1e-5 + pow(z, 4.0)), 1e-2, 3e3);
    float encoded = (log(weight) / 2.302585 + 2.0) / 5.5;
    FragColor = vec4(clamp(encoded, 0.0, 1.0), 0.0, 0.0, 1.0);
}
