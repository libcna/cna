// Shader Model 5.0 (ps_5_0). skinned3d_vertexlit.frag.hlsl's own stride-56 sibling: consumes the
// already-computed LitRGB/SpecularRGB/Alpha/Color varyings from
// skinned_colored3d_vertexlit.vert.hlsl instead of recomputing Blinn-Phong per pixel. Ported from
// EasyGLGraphicsBackend::EnsureSkinnedVertexLitProgram()'s fragment stage (CNB-67, Phase 13C).
//
// Same "multiply after specular add" discipline as skinned_colored3d.frag.hlsl -- see that file's
// own doc comment for the historical bug this avoids.

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
    float4 FogColor;
    float4 FogVector;
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
    float4 EmissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
};

struct PSInput
{
    float4 Position    : SV_Position;
    float2 UV          : TEXCOORD0;
    float  FogFactor   : TEXCOORD1;
    float3 LitRGB      : TEXCOORD2;
    float3 SpecularRGB : TEXCOORD3;
    float  Alpha       : TEXCOORD4;
    float4 Color       : TEXCOORD5;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = (TextureEnabled > 0.5) ? uTexture.Sample(uTextureSampler, input.UV) : float4(1.0, 1.0, 1.0, 1.0);
    float4 vc = (VertexColorEnabled > 0.5) ? input.Color : float4(1.0, 1.0, 1.0, 1.0);

    float4 outColor = float4(input.LitRGB * tex.rgb, input.Alpha * tex.a * vc.a);
    outColor.rgb += input.SpecularRGB * outColor.a;
    outColor.rgb *= vc.rgb;
    outColor.rgb = lerp(FogColor.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
