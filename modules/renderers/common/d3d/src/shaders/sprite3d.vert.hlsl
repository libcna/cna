// Shader Model 5.0 (vs_5_0). SpriteBatch's stock vertex stage with XNA's full semantics.
//
// sprite2d.vert.hlsl maps a pixel-space (x, y) straight to NDC and writes z = 0, w = 1, so layerDepth
// never reached the depth test, a Begin(transformMatrix) could only ever be applied as a 2D affine
// map on the CPU, and its third and fourth columns -- depth, W, perspective -- were thrown away.
// This is FNA's SpriteEffect vertex stage instead: the untransformed (x, y, layerDepth) goes through
// MatrixTransform = transformMatrix * CreateOrthographicOffCenter(0, w, h, 0, 0, -1), exactly as
// SpriteBatch.cs builds it, so z and w come out of the matrix and D3D11 does the rest -- clipping
// at 0 <= z <= w, the viewport depth range, and perspective-correct interpolation.
//
// The orthographic part reproduces sprite2d's x/y mapping exactly (x/w*2-1, 1-y/h*2) and passes z
// through unchanged, so a sprite drawn with an identity transform lands on the same pixels as
// before. The output matches sprite2d.frag.hlsl's input, which is reused unchanged.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 MatrixTransform;
};

struct VSInput
{
    float3 Position : POSITION0;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Position = mul(float4(input.Position, 1.0), MatrixTransform);
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}
