// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/colored3d.frag.glsl.

// Task 899: this shader never samples a texture, but binding=0 (t0/s0) is still reserved in the
// shared colored3d/textured3d/colored_textured3d layout convention (mirroring the GLSL source's
// own note about a fallback white texture bound there) so all three pipelines can share one
// root-signature/input-layout shape. Only register(b1) (fog) is actually read here.
cbuffer FogParams : register(b1)
{
    float4 FogColorEnabled;  // xyz = FogColor, w = fogEnabled
    float4 FogStartEnd;      // x = fogStart, y = fogEnd, zw = unused
};

struct PSInput
{
    float4 Position  : SV_Position;
    float4 Color     : TEXCOORD0;
    float  FogFactor : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    float4 outColor = input.Color;
    // Task 899: mix toward FogColor as FogFactor -> 0 (matches the established Task 888 formula).
    outColor.rgb = lerp(FogColorEnabled.xyz, outColor.rgb, input.FogFactor);
    return outColor;
}
