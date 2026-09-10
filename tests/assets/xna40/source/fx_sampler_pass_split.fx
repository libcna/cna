// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-201: two passes, each binding a different sampler,
// one carrying state and the other a texture.
//
// Both header dwords are the *pass's* own summary rather than the effect's: the first pass must
// answer the sampler-state bit and no texture, the second a texture and no sampler-state bit.
texture Surface;

sampler FilteredSampler : register(s0) = sampler_state { MinFilter = Point; };
sampler TexturedSampler : register(s1) = sampler_state { Texture = <Surface>; };

float4 FilteredMain(float2 texCoord : TEXCOORD0) : COLOR0
{
    return tex2D(FilteredSampler, texCoord);
}

float4 TexturedMain(float2 texCoord : TEXCOORD0) : COLOR0
{
    return tex2D(TexturedSampler, texCoord);
}

technique Only
{
    pass Filtered
    {
        PixelShader = compile ps_2_0 FilteredMain();
    }

    pass Textured
    {
        PixelShader = compile ps_2_0 TexturedMain();
    }
}
