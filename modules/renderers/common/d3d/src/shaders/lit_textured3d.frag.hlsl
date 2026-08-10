// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/lit_textured3d.frag.glsl.

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

// Task 897/886/898: DirectionalLight1/DirectionalLight2 + EmissiveColor + specular, forwarded via
// a second constant buffer since PerDraw above is already fully packed.
cbuffer LitLightParams : register(b1)
{
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 EmissiveColorPad;
    row_major float4x4 World;   // vertex-stage only; unused here
    float4 EyePosPad;
    float4 Light0SpecularPad;
    float4 Light1SpecularPad;
    float4 Light2SpecularPad;
    float4 SpecularColorPower;  // xyz = material SpecularColor, w = SpecularPower
    // Task 888: fog, packed into the constant buffer's previously-unused trailing 32 bytes.
    // Vertex-stage computes FogFactor from FogVector; only FogColor.xyz
    // needed here.
    float4 FogColor;     // xyz = FogColor, w = reserved padding
    float4 FogVector;         // CPU-prepared FNA view-space fog vector
};

struct PSInput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float3 Normal    : TEXCOORD1;
    float4 Tint      : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = (TextureEnabled > 0.5) ? uTexture.Sample(uTextureSampler, input.UV) : float4(1.0, 1.0, 1.0, 1.0);

    float4 color;
    if (LightingEnabled > 0.5)
    {
        float3 N = normalize(input.Normal);
        float3 E = normalize(EyePosPad.xyz - input.WorldPos);
        float3 nL0 = normalize(Light0Dir);
        float3 nL1 = normalize(Light1DirPad.xyz);
        float3 nL2 = normalize(Light2DirPad.xyz);
        // Direction fields point FROM the light, so negate for dot with N.
        float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
        float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
        float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
        float3 lightSum = AmbientColor + NdotL0 * Light0Diffuse
                         + NdotL1 * Light1DiffusePad.xyz + NdotL2 * Light2DiffusePad.xyz;
        // Half-vector Blinn-Phong specular (FNA's Lighting.fxh ComputeLights), gated by the same
        // zeroL "does this light face the surface" term used for diffuse. Material SpecularColor
        // is applied once to the summed per-light contribution, not per-light.
        float3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, SpecularColorPower.w);
        float3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, SpecularColorPower.w);
        float3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, SpecularColorPower.w);
        float3 specularRGB = (spec0 * Light0SpecularPad.xyz + spec1 * Light1SpecularPad.xyz
                              + spec2 * Light2SpecularPad.xyz) * SpecularColorPower.xyz;
        // EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it
        // (matches FNA's Lighting.fxh: result.Diffuse = sum*DiffuseColor + EmissiveColor).
        float3 lit = lightSum * input.Tint.rgb + EmissiveColorPad.xyz;
        color = float4(lit, input.Tint.a) * tex;
        // Specular is added after the texture*diffuse multiply, scaled by the resulting alpha
        // (FNA's AddSpecular macro), never by the texture directly.
        color.rgb += specularRGB * color.a;
    }
    else
    {
        color = input.Tint * tex;
    }
    // Task 888: mix toward FogColor as FogFactor -> 0 (matches EasyGL's established formula).
    color.rgb = lerp(FogColor.xyz, color.rgb, input.FogFactor);
    return color;
}
