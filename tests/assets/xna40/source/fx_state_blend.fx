// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191: one arm of the state-group experiment. XNA writes
// two dwords into its own effect-container header, and these fixtures each assign exactly one
// kind of state so that what those dwords count can be read off rather than guessed.
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
        SrcBlend = SrcAlpha;
        DestBlend = InvSrcAlpha;
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
