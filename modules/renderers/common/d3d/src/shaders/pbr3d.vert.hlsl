// Shader Model 5.0 (vs_5_0). Physically-based (metallic-roughness) unskinned pipeline -- HLSL
// port of EasyGLRenderer::EnsurePbrProgram()'s vertex stage (plans/plan_cnj.md CNB-58, PbrEffect).
// Stride 48: VertexPositionNormalTangentTexture (float3 pos + float3 normal + float4 tangent
// [xyz + bitangent handedness sign in w, glTF convention] + float2 uv). CNA_PBR_DUAL_UV adds
// the canonical stride-60 TEXCOORD_1 suffix.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    row_major float4x4 World;
    float4 DiffuseColor;       // material base color factor (RGBA)
    float4 AmbientMetallic;    // xyz = AmbientColor, w = MetallicFactor
    float4 EmissiveRoughness;  // xyz = EmissiveColor, w = RoughnessFactor
};

cbuffer PbrLights : register(b1)
{
    float4 EyePosWeights;   // xyz = EyePosition, w = WeightsPerVertex (unused here, unskinned)
    float4 Light0DirPad;
    float4 Light0DiffusePad;
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 FogColor; // xyz = FogColor, w = reserved padding
    float4 FogVector;     // CPU-prepared FNA view-space fog vector
};

struct VSInput
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Tangent  : TANGENT0;
    float2 UV       : TEXCOORD0;
#ifdef CNA_PBR_DUAL_UV
    float2 UV1      : TEXCOORD1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    float4 Color    : COLOR0;
#endif
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 Tangent   : TEXCOORD1;  // xyz = world-space tangent, w = bitangent handedness sign
    float2 UV        : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
    float3 WorldPos  : TEXCOORD4;
#ifdef CNA_PBR_DUAL_UV
    float2 UV1       : TEXCOORD5;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    float4 Color     : TEXCOORD6;
#endif
};

// Returns transpose(inverse(m)) directly (the cofactor matrix over the determinant) -- reuses the
// same helper lit_textured3d.vert.hlsl/env_map3d.vert.hlsl already established for this renderer's
// own normal-matrix convention (design decision: HLSL has no built-in inverse()).
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    output.Position = mul(float4(input.Position, 1.0), Mvp);

    float3x3 normalMatrix = InverseTranspose3x3((float3x3)World);
    output.Normal = normalize(mul(input.Normal, normalMatrix));

    // Tangent transforms as a plain direction under World (not the inverse-transpose used for the
    // normal above) -- correct for uniform-scale World transforms, matching PbrEffect's EasyGL
    // reference (EnsurePbrProgram) exactly; a documented simplification shared with most real-time
    // engines lacking a full per-tangent inverse-transpose.
    float3x3 worldDirectionMat = (float3x3)World;
    output.Tangent = float4(mul(input.Tangent.xyz, worldDirectionMat),
                            input.Tangent.w * CnaDirectionHandedness(worldDirectionMat));

    output.UV = input.UV;
#ifdef CNA_PBR_DUAL_UV
    output.UV1 = input.UV1;
#endif
#ifdef CNA_PBR_VERTEX_COLOR
    output.Color = input.Color;
#endif
    output.WorldPos = mul(float4(input.Position, 1.0), World).xyz;

    // Matches this renderer's own established fog-factor formula (lit_textured3d.vert.hlsl/
    // skinned3d.vert.hlsl) -- not EasyGL's own differently-derived formula (Task 1111's own note);
    // this file follows the D3D11 renderer's existing convention, same "don't invent a new one"
    // discipline the rest of this port already applies.
    // REMED-GFX-005/010: FNA view-space fog. FogVector now carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
