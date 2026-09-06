// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191: every blend state this corpus classifies, in one
// pass, so the container's dense numbering for each is read rather than assumed and the group bit
// they all produce is confirmed on more than one member.
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
        AlphaBlendEnable = true;
        SeparateAlphaBlendEnable = true;
        SrcBlend = SrcAlpha;
        DestBlend = InvSrcAlpha;
        BlendOp = Add;
        SrcBlendAlpha = One;
        DestBlendAlpha = Zero;
        BlendOpAlpha = Add;
        ColorWriteEnable = Red | Green | Blue;
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
