// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191/192: fx_minimal.fx plus one initialized parameter.
float4x4 WorldViewProjection;
float4 Tint = float4(0.25, 0.5, 0.75, 1);

float4 VertexMain(float4 position : POSITION0) : POSITION0
{
    return mul(position, WorldViewProjection);
}

float4 PixelMain() : COLOR0
{
    return Tint;
}

technique Only
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
