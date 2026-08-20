// Shader Model 5.0 (vs_5_0). Sibling of alpha_test3d.vert.hlsl (stride 20, Position+UV only) for
// stride-24 draws (VertexPositionColorTexture) -- AlphaTestEffect.VertexColorEnabled needs a real
// vertex-color attribute to multiply against, which the plain alpha_test3d variant deliberately
// doesn't carry (plans/plan_dx.md DX-136). Mixes vertex color and diffuse exactly like
// colored_textured3d.vert.hlsl's own established VertexColorEnabled pattern; the pixel shader
// (alpha_test_colored3d.frag.hlsl) is otherwise identical to alpha_test3d.frag.hlsl's alpha-test
// logic, since it only reads the already-combined Tint, not the vertex color directly.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;    // offset  0  (64 bytes)
    float4 DiffuseColor;       // offset 64  (16 bytes)
    float4 AlphaTestParams;    // offset 80  (16 bytes) -- unused here, read only in the PS
    // REMED-GFX-005/010: FNA view-space fog vector (was VertexColorEnabled/FogEnabled/FogStart/FogEnd).
    float4 FogVector;          // offset 96
    float3 FogColor;           // offset 112
    float  VertexColorEnabled; // offset 124
};

struct VSInput
{
    float3 Position : POSITION0;
    float4 Color    : COLOR0;    // normalized UNORM R8G8B8A8
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Tint      : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 pos = mul(float4(input.Position, 1.0), Mvp);
    output.Position = pos;
    output.UV = input.UV;

    // Mix vertex color and diffuse based on VertexColorEnabled flag (matches
    // colored_textured3d.vert.hlsl's own established pattern).
    output.Tint = (VertexColorEnabled > 0.5) ? input.Color * DiffuseColor : DiffuseColor;

    // REMED-GFX-005/010: FNA view-space fog. keep = 1 - saturate(dot(objectPos, fogVector)); the
    // vector bakes World*View's 3rd column (eye-space Z), the corrected non-mirrored FNA factor.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
