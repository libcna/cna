// Shader Model 5.0 (ps_5_0). Sibling of alpha_test3d.frag.hlsl for the stride-24
// (VertexPositionColorTexture) alpha-test-colored variant (plans/plan_dx.md DX-136). The alpha-test
// logic itself is unchanged from alpha_test3d.frag.hlsl -- this shader only ever reads the
// already-combined Tint the vertex shader computed (input.Color * DiffuseColor when
// VertexColorEnabled), not the vertex color directly, so it needs no changes beyond the input
// struct/cbuffer staying byte-identical to alpha_test_colored3d.vert.hlsl's own PerDraw layout.

Texture2D    uTexture        : register(t0);
SamplerState uTextureSampler : register(s0);

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;    // offset  0
    float4 DiffuseColor;       // offset 64
    float  AlphaRef;           // offset 80 -- reference alpha value
    float  AlphaTol;           // offset 84 -- tolerance (>0 = equality test, 0 = comparison)
    float  AlphaPassW;         // offset 88 -- weight when test passes  (< 0 = discard)
    float  AlphaFailW;         // offset 92 -- weight when test fails   (< 0 = discard)
    // REMED-GFX-005/010: FNA view-space fog vector (used only by the VS; declared here for the
    // shared b0 layout). Replaces the former VertexColorEnabled/FogEnabled/FogStart/FogEnd quartet.
    float4 FogVector;          // offset 96
    float3 FogColor;           // offset 112
    float  VertexColorEnabled; // offset 124 -- unused here (read only in the VS)
};

struct PSInput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Tint      : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
};

// Alpha test uses the same encoding as alpha_test3d.frag.hlsl:
//   if (AlphaTol > 0) -> pass = |alpha - AlphaRef| < AlphaTol
//   else               -> pass = alpha < AlphaRef
//   weight = pass ? AlphaPassW : AlphaFailW
//   if weight < 0 -> discard
// Default {0,0,1,1} = always pass (never discard).
float4 main(PSInput input) : SV_Target
{
    float4 outColor = uTexture.Sample(uTextureSampler, input.UV) * input.Tint;

    float alpha = outColor.a;
    bool passTest;
    if (AlphaTol > 0.0)
    {
        passTest = abs(alpha - AlphaRef) < AlphaTol;
    }
    else
    {
        passTest = alpha < AlphaRef;
    }
    float w = passTest ? AlphaPassW : AlphaFailW;
    if (w < 0.0) discard;

    // Mix toward FogColor as FogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = lerp(FogColor, outColor.rgb, input.FogFactor);
    return outColor;
}
