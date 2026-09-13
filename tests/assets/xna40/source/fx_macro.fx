// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-192: an effect that branches on a preprocessor symbol.
//
// Nothing defines CNA_TINTED here, so a build with no macros takes the untinted arm. XNA's
// EffectProcessor has no parameter for defines and always compiles with none, which is what this
// case pins; CNA's own route has a `defines` parameter, and the two agree only when it is unset.
float4x4 WorldViewProjection;

float4 VertexMain(float4 position : POSITION0) : POSITION0
{
    return mul(position, WorldViewProjection);
}

float4 PixelMain() : COLOR0
{
#ifdef CNA_TINTED
    return float4(1, 0, 0, 1);
#else
    return float4(1, 1, 1, 1);
#endif
}

technique Only
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
