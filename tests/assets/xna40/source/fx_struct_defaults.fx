// SPDX-License-Identifier: MS-PL
//
// 2026-10-26 dependency audit, A2v2 A-09: a structure parameter whose non-array members precede an
// array member, the shape CnaConformanceEffect.fx's Lighting has. Every member is read by the
// vertex shader so the compiler keeps the whole structure, and XNA's own Content Pipeline builds
// it (tools/xna-pipeline-oracle/differential/effect_struct_defaults.json).
struct LightingParameters
{
    float Intensity;
    float3 Direction;
    float Thresholds[2];
};

LightingParameters Lighting = {
    0.5f,
    float3(0.6f, 0.7f, 0.8f),
    { 0.9f, 1.0f }
};

float4 VertexMain(float4 position : POSITION0) : POSITION0
{
    return position * Lighting.Intensity +
           float4(Lighting.Direction, Lighting.Thresholds[0] + Lighting.Thresholds[1]);
}

float4 PixelMain() : COLOR0
{
    return float4(1, 1, 1, 1);
}

technique Only
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
