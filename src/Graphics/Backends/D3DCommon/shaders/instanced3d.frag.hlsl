// Shader Model 5.0 (ps_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.frag.glsl.
// Plain flat instance-diffuse-color output: no fog, no texture (matches the GLSL source's own
// note that Instanced3D deliberately keeps a minimal single-binding layout shared with 2D
// SpriteBatch).

struct PSInput
{
    float4 Position : SV_Position;
    float4 Color    : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    return input.Color;
}
