// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191: one sampler, pinned to register s3.
//
// The discriminator for what XNA's second header dword holds. If it is a mask of the sampler
// registers an effect binds, this answers 8; if it merely counts samplers, it answers 1.
float4x4 WorldViewProjection;
texture Surface;

sampler SurfaceSampler : register(s3) = sampler_state
{
    Texture = <Surface>;
    MinFilter = Linear;
};

struct VertexOut
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
};

VertexOut VertexMain(float4 position : POSITION0, float2 texCoord : TEXCOORD0)
{
    VertexOut output;
    output.Position = mul(position, WorldViewProjection);
    output.TexCoord = texCoord;
    return output;
}

float4 PixelMain(VertexOut input) : COLOR0
{
    return tex2D(SurfaceSampler, input.TexCoord);
}

technique Only
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
