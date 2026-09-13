// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-192: an effect whose parameter and transform come from an
// included header, so the include has to be resolved for the effect to compile at all.
#include "fx_common.fxh"

float4 VertexMain(float4 position : POSITION0) : POSITION0
{
    return TransformToClip(position);
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
