// Shader Model 5.0 (ps_5_0). Per-vertex-lit sibling of skinned3d.frag.hlsl -- consumes the
// already-computed LitRGB/SpecularRGB/Alpha varyings from skinned3d_vertexlit.vert.hlsl instead
// of recomputing Blinn-Phong per pixel. Non-lighting math (texture sample, fog) is unchanged.

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

cbuffer FogParams : register(b2)
{
    float4 FogColorEnabled;
    float4 FogStartEnd;
    float4 Light1DirPad;
    float4 Light1DiffPad;
    float4 Light2DirPad;
    float4 Light2DiffPad;
    row_major float4x4 World;
    float4 EyePosPad;
    float4 SpecularColorPower;
    float4 Light0SpecPad;
    float4 Light1SpecPad;
    float4 Light2SpecPad;
};

struct PSInput
{
    float4 Position    : SV_Position;
    float2 UV          : TEXCOORD0;
    float  FogFactor   : TEXCOORD1;
    float3 LitRGB      : TEXCOORD2;
    float3 SpecularRGB : TEXCOORD3;
    float  Alpha       : TEXCOORD4;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = (TextureEnabled > 0.5) ? uTexture.Sample(uTextureSampler, input.UV) : float4(1.0, 1.0, 1.0, 1.0);
    float4 outColor = float4(input.LitRGB * tex.rgb, input.Alpha * tex.a);
    outColor.rgb += input.SpecularRGB * outColor.a;
    outColor.rgb = lerp(FogColorEnabled.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
