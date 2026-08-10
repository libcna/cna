// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/sprite2d.vert.glsl.
//
// D3D-specific deviation from the GLSL source (genuine, not a copy-paste of the Vulkan flip):
// the GLSL formula maps pixel-space Y (top-left origin, Y-down) directly onto Vulkan's own NDC,
// which is already Y-down at rasterization (unlike every 3D shader in this file set, which needed
// NO Vulkan-specific correction because they instead flip in the *opposite* direction to undo
// Vulkan's Y-down vs the D3D/GL Y-up convention XNA's projection matrices already target). Here
// there is no projection matrix to rely on -- this is a raw pixel->NDC formula -- so it must
// negate NDC.y explicitly for D3D11 (D3D's NDC is Y-up, viewport-flipped to Y-down on screen,
// same as OpenGL); omitting this flip would render sprites upside down.

cbuffer PerDraw : register(b0)
{
    float2 ViewportSize;
};

struct VSInput
{
    float2 Position : POSITION0;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // pixel-space -> NDC: (0,0) top-left, Y-down to match XNA. Y negated vs the GLSL source --
    // see the file header comment for why this is a genuine, required D3D-specific deviation.
    float2 ndc = (input.Position / ViewportSize) * 2.0 - 1.0;
    output.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);
    output.UV = input.UV;
    output.Color = input.Color;

    return output;
}
