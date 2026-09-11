// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191/192: the smallest effect the corpus has.
//
// One parameter, one technique, one pass, shader model 2.0, and nothing else: no texture, no
// sampler, no initializer, no state. Every other fixture in this sweep is this one plus exactly
// one feature, which is what makes a difference in the compiled container attributable.
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
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
