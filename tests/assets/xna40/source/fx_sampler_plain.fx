// SPDX-License-Identifier: MS-PL
//
// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-201: a sampler the shader declares and the effect
// assigns nothing to.
//
// The discriminator between "the sampler registers the pass's shaders bind", which is what CNA
// used to write, and "the sampler registers the effect binds a texture into", which is what XNA
// writes. The shader declares s0 either way; only the second answers zero.
sampler SurfaceSampler : register(s0);

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
