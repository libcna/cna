// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-192: fx_minimal.fx with a second technique.
float4x4 WorldViewProjection;

float4 VertexMain(float4 position : POSITION0) : POSITION0
{
    return mul(position, WorldViewProjection);
}

float4 White() : COLOR0 { return float4(1, 1, 1, 1); }
float4 Black() : COLOR0 { return float4(0, 0, 0, 1); }

technique First
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 White();
    }
}

technique Second
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 Black();
    }
}
