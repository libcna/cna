// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl.

Texture2D    uTexture         : register(t0);
SamplerState uTextureSampler  : register(s0);
TextureCube  uEnvMap          : register(t1);
SamplerState uEnvMapSampler   : register(s1);

cbuffer EnvMapParams : register(b2)
{
    float4 EyePosPad;
    float4 DiffuseColor;
    float4 EmissiveEm;           // xyz = emissive, w = envMapAmount
    float4 Light0DirPad;         // xyz = light dir, w = pad
    float4 Light0DiffFresnelEn;  // xyz = light diffuse, w = fresnelEnabled (0/1)
    float4 EnvMapSpecFresnelF;   // xyz = envMapSpecular, w = fresnelFactor
    // Task 899's noted cheap leftover: fog packed into this constant buffer's spare tail bytes.
    float4 FogColor;      // xyz = FogColor, w = reserved padding
    float4 FogVector;          // CPU-prepared FNA view-space fog vector
    // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    float4 Light1DirPad;
    float4 Light1DiffPad;
    float4 Light2DirPad;
    float4 Light2DiffPad;
};

struct PSInput
{
    float4 Position     : SV_Position;
    float3 WorldNormal  : TEXCOORD0;
    float3 EyeDir       : TEXCOORD1;
    float2 UV           : TEXCOORD2;
    float  FogFactor    : TEXCOORD3;
};

float4 main(PSInput input) : SV_Target
{
    float3 N = normalize(input.WorldNormal);
    float3 E = normalize(input.EyeDir);
    float NdotL0 = max(dot(N, -Light0DirPad.xyz), 0.0);
    float NdotL1 = max(dot(N, -Light1DirPad.xyz), 0.0);
    float NdotL2 = max(dot(N, -Light2DirPad.xyz), 0.0);
    float3 lightSum = Light0DiffFresnelEn.xyz * NdotL0
                     + Light1DiffPad.xyz * NdotL1
                     + Light2DiffPad.xyz * NdotL2;
    // REMED-GFX-007: FNA Lighting.fxh adds EmissiveColor UNSCALED (the CPU layer already pre-folds
    // ambient into it): litRGB = lightSum*DiffuseColor + Emissive. The prior
    // (Emissive + lightSum) * DiffuseColor re-scaled the emissive by DiffuseColor -- invisible only
    // when DiffuseColor is white. Matches the corrected Vulkan/SdlGpu/Bgfx/WebGPU env_map3d shaders.
    float3 litRGB = lightSum * DiffuseColor.rgb + EmissiveEm.xyz;
    float4 texColor = uTexture.Sample(uTextureSampler, input.UV);
    float3 reflDir = reflect(-E, N);
    float4 envSample = uEnvMap.Sample(uEnvMapSampler, reflDir);
    float3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = DiffuseColor.a * texColor.a;
    float viewAngle = dot(E, N);
    float blendFactor = (Light0DiffFresnelEn.w > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), EnvMapSpecFresnelF.w) * EmissiveEm.w
        : EmissiveEm.w;
    // Task 891: FNA's real PSEnvMap/PSEnvMapSpecular scale the whole `envmap` sample (both the
    // base lerp target and the specular term, the latter already fixed by Task 395) by
    // combinedAlpha before use -- the base lerp's envSample.rgb was still unscaled here.
    float3 rgb = lerp(baseColor, envSample.rgb * combinedAlpha, blendFactor) + EnvMapSpecFresnelF.xyz * envSample.a * combinedAlpha;
    // Mix toward FogColor as FogFactor -> 0 (matches the established Task 888 formula).
    rgb = lerp(FogColor.xyz, rgb, input.FogFactor);
    return float4(rgb, combinedAlpha);
}
