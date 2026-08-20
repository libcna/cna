// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-004: CNA's own compiled-effect conformance source.
//
// Everything else in the compiled-effect suite is either a stock binary Microsoft compiled years
// ago, or a container CNA assembles itself byte by byte. Both are useful and neither is compiler
// output for a source this project controls -- so no test could ask "does a real Effect compiler,
// given these declarations, produce reflection CNA reads correctly?"
//
// This file is that source. It deliberately declares one of each thing the reflection contract
// cares about: scalars with and without a semantic, a vector, a column-major matrix, an array, a
// structure with mixed member shapes, annotations on a parameter, a technique and a pass, two
// techniques, three passes, a texture with its sampler and explicit sampler states, and pass
// render states.
//
// Compile with the same Effect compiler XNA used (see this directory's README for the exact
// command and the recorded hash):
//
//   fxc /T fx_2_0 /Fo CnaConformanceEffect.fxb CnaConformanceEffect.fx

float Gain : SCALAR
<
    bool Visible = true;
> = 0.25f;

float4 Tint = float4(0.1f, 0.2f, 0.3f, 0.4f);

float4x4 Transform = float4x4(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
);

float Weights[2] = { 0.2f, 0.8f };

struct LightingParameters
{
    float Intensity;
    float3 Direction;
    float Thresholds[2];
};

LightingParameters Lighting = {
    0.5f,
    float3(0.6f, 0.7f, 0.8f),
    { 0.9f, 1.0f }
};

texture FxTexture;

sampler FxSampler = sampler_state
{
    Texture = (FxTexture);
    MinFilter = Point;
    MagFilter = Linear;
    MipFilter = Point;
    AddressU = Mirror;
    AddressV = Clamp;
    MaxAnisotropy = 8;
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

VertexOut MainVertexShader(VertexIn input)
{
    VertexOut output;
    output.Position = mul(input.Position, Transform);
    output.TexCoord = input.TexCoord;
    return output;
}

float4 MainPixelShader(VertexOut input) : COLOR0
{
    float4 sampled = tex2D(FxSampler, input.TexCoord);
    float lighting = saturate(Lighting.Intensity + Lighting.Thresholds[0]);
    return sampled * Tint * Gain * lighting;
}

float4 FlatPixelShader(VertexOut input) : COLOR0
{
    return Tint * Weights[1];
}

technique FirstTechnique
<
    int Quality = 7;
>
{
    pass P0
    {
        VertexShader = compile vs_2_0 MainVertexShader();
        PixelShader = compile ps_2_0 MainPixelShader();
    }

    pass StatePass
    <
        int PassTag = 3;
    >
    {
        ZEnable = false;
        ZWriteEnable = false;
        CullMode = None;
        AlphaBlendEnable = true;
        SrcBlend = SrcAlpha;
        DestBlend = InvSrcAlpha;
        BlendOp = Add;
        VertexShader = compile vs_2_0 MainVertexShader();
        PixelShader = compile ps_2_0 FlatPixelShader();
    }
}

technique SecondTechnique
{
    pass P1
    {
        VertexShader = compile vs_2_0 MainVertexShader();
        PixelShader = compile ps_2_0 FlatPixelShader();
    }
}
