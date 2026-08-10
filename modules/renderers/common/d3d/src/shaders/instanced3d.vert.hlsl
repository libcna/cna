// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.vert.glsl.
//
// Per-vertex attribute (input slot 0, per-vertex step rate): only Position is consumed by this
// shader; the per-vertex stride may be 16/20/24/32 -- extra interleaved attributes the input
// layout provides (color/uv/normal) are simply not declared in VSInput below and go unread,
// exactly like the GLSL source.
//
// Per-instance world matrix (input slot 1, per-instance step rate, stride = 64): one float4 "row"
// per semantic, INSTANCEWORLD0..3 -- a project-local semantic name (D3D11_INPUT_ELEMENT_DESC for
// this instanced input layout is Phase DX5 scope, not yet written; whoever wires it up must use
// these exact 4 semantic names/indices to match this shader's input signature).

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Vp;
    float4 DiffuseColor;
    float3 AmbientColor;   float LightingEnabled;
    float3 Light0Dir;      float TextureEnabled;
    float3 Light0Diffuse;  float VertexColorEnabled;
};

struct VSInput
{
    float3 Position   : POSITION0;
    float4 InstCol0   : INSTANCEWORLD0;
    float4 InstCol1   : INSTANCEWORLD1;
    float4 InstCol2   : INSTANCEWORLD2;
    float4 InstCol3   : INSTANCEWORLD3;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float4 Color    : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // GLSL builds `world` from 4 column vectors (mat4(col0,col1,col2,col3)); the equivalent HLSL
    // row-major construction from 4 row vectors preserves the same combined-transform result under
    // this file set's "GLSL M*v -> HLSL mul(v,M)" translation rule (see colored3d.vert.hlsl).
    float4x4 world = float4x4(input.InstCol0, input.InstCol1, input.InstCol2, input.InstCol3);
    float4 worldPos = mul(float4(input.Position, 1.0), world);
    output.Position = mul(worldPos, Vp);
    output.Color = DiffuseColor;

    return output;
}
