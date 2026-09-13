// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-201: a sampler that assigns filters and no texture.
//
// Splits the two halves of what a sampler contributes. A `Texture` assignment is a binding and
// reaches the second header dword; anything else is device sampler state and reaches the first,
// three places up from the state groups. This effect has the second kind and not the first.
sampler SurfaceSampler : register(s0) = sampler_state
{
    MinFilter = Linear;
    MagFilter = Linear;
};

float4 PixelMain(float2 texCoord : TEXCOORD0) : COLOR0
{
    return tex2D(SurfaceSampler, texCoord);
}

technique Only
{
    pass Single
    {
        PixelShader = compile ps_2_0 PixelMain();
    }
}
