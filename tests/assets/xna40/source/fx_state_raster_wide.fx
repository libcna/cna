// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191: every rasterizer state this corpus classifies.
float4x4 WorldViewProjection;

float4 VertexMain(float4 position : POSITION0) : POSITION0
{
    return mul(position, WorldViewProjection);
}

float4 PixelMain() : COLOR0
{
    return float4(1, 1, 1, 1);
}

technique Only
{
    pass Single
    {
        CullMode = CCW;
        FillMode = Solid;
        ScissorTestEnable = true;
        DepthBias = 0.0f;
        SlopeScaleDepthBias = 0.0f;
        MultiSampleAntialias = true;
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
