// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/instanced3d.vert.glsl.
//
// The base variant consumes Position only. CNA_INSTANCED_VERTEX_COLOR_INPUT adds COLOR0 so
// BasicEffect.VertexColorEnabled has the same meaning on ordinary and instanced draw routes.
//
// The public TextureCoordinate1..4 instance-matrix columns map to INSTANCEWORLD0..3 here. DX-222
// assigns each column's native slot and step rate from its VertexBufferBinding, so the four columns
// may occupy one stream or several without changing this shader signature.

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
#ifdef CNA_INSTANCED_VERTEX_COLOR_INPUT
    float4 Color      : COLOR0;
#endif
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
#ifdef CNA_INSTANCED_VERTEX_COLOR_INPUT
    if (VertexColorEnabled != 0.0)
        output.Color *= input.Color;
#endif

    return output;
}
