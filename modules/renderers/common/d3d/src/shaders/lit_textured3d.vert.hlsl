// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/lit_textured3d.vert.glsl.
// Stride 32: VertexPositionNormalTexture -- float3 pos + float3 normal + float2 uv.

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

// Task 897/886/898: DirectionalLight1/2 + EmissiveColor + specular data, forwarded via a second
// constant buffer since PerDraw above is already fully packed. World lives here (not in PerDraw)
// purely so this vertex shader can compute a correct world-space position/normal.
cbuffer LitLightParams : register(b1)
{
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 EmissiveColorPad;
    row_major float4x4 World;
    float4 EyePosPad;
    float4 Light0SpecularPad;
    float4 Light1SpecularPad;
    float4 Light2SpecularPad;
    float4 SpecularColorPower;
    // Task 888: fog, packed into the constant buffer's previously-unused trailing 32 bytes.
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
};

struct VSInput
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float2 UV        : TEXCOORD0;
    float3 Normal    : TEXCOORD1;  // world-space
    float4 Tint      : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
};

// Returns transpose(inverse(m)) directly (the cofactor matrix over the determinant) -- see
// plans/plan_dx.md DX-13-hlsl's task notes: HLSL has no built-in inverse()/GLSL-style mat3(mat4)
// equivalent, so this is a deliberate, documented deviation from the 1:1 GLSL port.
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 pos = mul(float4(input.Position, 1.0), Mvp);
    output.Position = pos;
    output.UV = input.UV;

    // Task 898 fix: transform by World's inverse-transpose upper-left 3x3, not the full MVP
    // (mirrors EnvironmentMapEffect's own already-correct env_map3d.vert.hlsl pattern) -- an
    // MVP-based transform bakes View/Projection into the normal, wrong under any non-identity
    // camera, not just non-uniform World scale.
    float3x3 normalMatrix = InverseTranspose3x3((float3x3)World);
    output.Normal = normalize(mul(input.Normal, normalMatrix));
    output.WorldPos = mul(float4(input.Position, 1.0), World).xyz;
    output.Tint = DiffuseColor;

    // REMED-GFX-005/010/061: FNA view-space fog. FogVector carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
