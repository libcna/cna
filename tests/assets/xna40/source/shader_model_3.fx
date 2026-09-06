// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-267: an effect only one of the two profiles can take.
//
// Reach is shader model 2.0; HiDef is 3.0. Nothing else about this source is unusual, so a build
// that refuses it refuses it for the target rather than for the code.
float4x4 WorldViewProjection;

struct VertexIn
{
    float4 Position : POSITION0;
};

struct VertexOut
{
    float4 Position : POSITION0;
};

VertexOut VertexMain(VertexIn input)
{
    VertexOut output;
    output.Position = mul(input.Position, WorldViewProjection);
    return output;
}

float4 PixelMain() : COLOR0
{
    return float4(1, 1, 1, 1);
}

technique ShaderModelThree
{
    pass Single
    {
        VertexShader = compile vs_3_0 VertexMain();
        PixelShader = compile ps_3_0 PixelMain();
    }
}
