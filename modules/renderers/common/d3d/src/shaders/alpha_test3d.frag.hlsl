// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/alpha_test3d.frag.glsl.

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
    float  VertexColorEnabled; // offset 124 -- unused here (read only in the colored VS)
};

struct PSInput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float4 Tint      : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
};

// Alpha test uses the same encoding as EasyGL:
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

    // Task 888: mix toward FogColor as FogFactor -> 0 (matches EasyGL's established formula).
    outColor.rgb = lerp(FogColor, outColor.rgb, input.FogFactor);
    return outColor;
}
