// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-191: two samplers, so a header field that counts them can
// be told from one that merely records that there are any.
float4x4 WorldViewProjection;
texture First;
texture Second;

sampler FirstSampler = sampler_state { Texture = <First>; MinFilter = Linear; };
sampler SecondSampler = sampler_state { Texture = <Second>; MinFilter = Point; };

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
    return tex2D(FirstSampler, input.TexCoord) * tex2D(SecondSampler, input.TexCoord);
}

technique Only
{
    pass Single
    {
        CullMode = None;
        VertexShader = compile vs_2_0 VertexMain();
        PixelShader = compile ps_2_0 PixelMain();
    }
}
