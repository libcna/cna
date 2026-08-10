// Shader Model 5.0 (ps_5_0). Per-vertex-lit sibling of lit_textured3d.frag.hlsl -- consumes the
// already-computed LitRGB/SpecularRGB varyings from lit_textured3d_vertexlit.vert.hlsl instead of
// recomputing Blinn-Phong per pixel. Non-lighting math (texture sample, fog) is unchanged.

Texture2D    uTexture        : register(t0);
SamplerState uTextureSampler : register(s0);

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    float4 DiffuseColor;
    float3 AmbientColor;
    float  LightingEnabled;
    float3 Light0Dir;
    float  TextureEnabled;
    float3 Light0Diffuse;
    float  VertexColorEnabled;
};

cbuffer LitLightParams : register(b1)
{
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 EmissiveColorPad;
    row_major float4x4 World;
    float4 EyePosPad;
    float4 Light0SpecularPad;
    float4 Light1SpecularPad;
    float4 Light2SpecularPad;
    float4 SpecularColorPower;
    float4 FogColor;
    float4 FogVector;
};

struct PSInput
{
    float4 Position    : SV_Position;
    float2 UV          : TEXCOORD0;
    float4 Tint        : TEXCOORD1;
    float  FogFactor   : TEXCOORD2;
    float3 LitRGB      : TEXCOORD3;
    float3 SpecularRGB : TEXCOORD4;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = (TextureEnabled > 0.5) ? uTexture.Sample(uTextureSampler, input.UV) : float4(1.0, 1.0, 1.0, 1.0);

    // Matches lit_textured3d.frag.hlsl's own lit branch exactly: LitRGB/SpecularRGB already carry
    // what that shader recomputed per-pixel from lightSum*Tint+Emissive / the half-vector terms.
    float4 color = float4(input.LitRGB, input.Tint.a) * tex;
    color.rgb += input.SpecularRGB * color.a;

    color.rgb = lerp(FogColor.xyz, color.rgb, input.FogFactor);
    return color;
}
