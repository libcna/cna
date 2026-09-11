// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-201: two samplers one pass binds, one of which the
// effect assigns a texture to.
//
// The shader declares s0 and s1; only s1 is the effect's own binding. A mask taken from the
// shader answers 3 and a mask taken from the effect answers 2.
texture Second;

sampler FirstSampler : register(s0);
sampler SecondSampler : register(s1) = sampler_state { Texture = <Second>; };

float4 PixelMain(float2 texCoord : TEXCOORD0) : COLOR0
{
    return tex2D(FirstSampler, texCoord) + tex2D(SecondSampler, texCoord);
}

technique Only
{
    pass Single
    {
        PixelShader = compile ps_2_0 PixelMain();
    }
}
