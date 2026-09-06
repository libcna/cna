// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-021: the committed .fx source.
//
// One technique with one pass, a matrix parameter, a texture and a sampler: the smallest effect
// that still exercises every kind of reflection an EffectProcessor reports.
float4x4 WorldViewProjection;
float4 Tint = float4(1, 1, 1, 1);
texture Surface;

sampler SurfaceSampler = sampler_state
{
    Texture = <Surface>;
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
};

struct VertexIn
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
};

struct VertexOut
{
    float4 Position : POSITION0;
    float2 TexCoord : TEXCOORD0;
};

VertexOut VertexMain(VertexIn input)
{
    VertexOut output;
    output.Position = mul(input.Position, WorldViewProjection);
    output.TexCoord = input.TexCoord;
    return output;
}

float4 PixelMain(VertexOut input) : COLOR0
{
    return tex2D(SurfaceSampler, input.TexCoord) * Tint;
}

technique Textured
{
    pass Single
    {
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
