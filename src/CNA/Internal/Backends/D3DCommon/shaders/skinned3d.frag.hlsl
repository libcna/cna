// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/skinned3d.frag.glsl.

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
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
    // Task 893: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    float4 Light1DirPad;
    float4 Light1DiffPad;
    float4 Light2DirPad;
    float4 Light2DiffPad;
    // Task 894: World matrix (unused here, vertex-shader only) + EyePosition + specular.
    row_major float4x4 World;
    float4 EyePosPad;
    float4 SpecularColorPower; // xyz = SpecularColor, w = SpecularPower
    float4 Light0SpecPad;
    float4 Light1SpecPad;
    float4 Light2SpecPad;
    float4 EmissiveColor;   // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha
};

struct PSInput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float2 UV        : TEXCOORD1;
    float  FogFactor : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
};

float4 main(PSInput input) : SV_Target
{
    float3 N = normalize(input.Normal);
    float3 E = normalize(EyePosPad.xyz - input.WorldPos);
    float dotL0 = dot(N, -normalize(Light0Dir));       float zeroL0 = step(0.0, dotL0);       float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -normalize(Light1DirPad.xyz)); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -normalize(Light2DirPad.xyz)); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = Light0Diffuse * NdotL0
                     + Light1DiffPad.xyz * NdotL1
                     + Light2DiffPad.xyz * NdotL2;
    float3 litRGB = lightSum * DiffuseColor.rgb + EmissiveColor.rgb;
    float specularPower = SpecularColorPower.w;
    float3 h0 = normalize(E - normalize(Light0Dir));       float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, specularPower);
    float3 h1 = normalize(E - normalize(Light1DirPad.xyz)); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, specularPower);
    float3 h2 = normalize(E - normalize(Light2DirPad.xyz)); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, specularPower);
    float3 specularRGB = (spec0 * Light0SpecPad.xyz + spec1 * Light1SpecPad.xyz
                          + spec2 * Light2SpecPad.xyz) * SpecularColorPower.xyz;
    float4 tex = (TextureEnabled > 0.5) ? uTexture.Sample(uTextureSampler, input.UV) : float4(1.0, 1.0, 1.0, 1.0);
    float4 outColor = float4(litRGB * tex.rgb, DiffuseColor.a * tex.a);
    outColor.rgb += specularRGB * outColor.a;
    // Task 899: mix toward FogColor as FogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = lerp(FogColor.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
