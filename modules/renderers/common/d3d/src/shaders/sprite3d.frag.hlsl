// Shader Model 5.0 (ps_5_0). WINCLOSE-0019: sprite2d.frag.hlsl plus Direct3D 9's texture-format
// expansion, for the stock SpriteBatch stage that sprite3d.vert.hlsl feeds.
//
// XNA runs on Direct3D 9, which gives a shader (R, 1, 1, 1) for a one-channel format and
// (R, G, 1, 1) for a two-channel one. Direct3D 10+ gives (R, 0, 0, 1) and (R, G, 0, 1), so a
// SurfaceFormat.Single shadow map drawn with SpriteBatch came out red instead of the grey
// ramp XNA shows. The mask/fill pair per format is EasyGL's (EasyGLSpriteBatchRenderer::
// ApplyChannelExpansion); a format that stores four channels takes the identity pair.
//
// A separate shader rather than an edit to sprite2d.frag.hlsl: that DXBC is also Direct3D 12's
// sprite pixel shader, whose root signature binds no pixel-stage constant buffer.

Texture2D    texSampler        : register(t0);
SamplerState texSamplerSampler : register(s0);

cbuffer ChannelExpansion : register(b0)
{
    float4 ChannelMask;
    float4 ChannelFill;
};

struct PSInput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    return (texSampler.Sample(texSamplerSampler, input.UV) * ChannelMask + ChannelFill) * input.Color;
}
