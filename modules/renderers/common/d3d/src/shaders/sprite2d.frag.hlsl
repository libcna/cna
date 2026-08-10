// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/sprite2d.frag.glsl.

Texture2D    texSampler        : register(t0);
SamplerState texSamplerSampler : register(s0);

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    return texSampler.Sample(texSamplerSampler, input.UV) * input.Color;
}
