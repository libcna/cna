// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191: every depth and stencil state this corpus
// classifies, for the same reason as fx_state_blend_wide.fx.
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
        ZEnable = true;
        ZWriteEnable = true;
        ZFunc = LessEqual;
        StencilEnable = true;
        StencilFunc = Always;
        StencilPass = Keep;
        StencilFail = Keep;
        StencilZFail = Keep;
        StencilRef = 1;
        StencilMask = 255;
        StencilWriteMask = 255;
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
