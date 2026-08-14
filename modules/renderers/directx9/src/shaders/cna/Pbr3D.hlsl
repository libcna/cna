// SPDX-License-Identifier: MS-PL
// D3D9 PBR porting task: CNA's own CNAEXT "Pbr3D" shader -- PbrEffect (unskinned metallic-roughness
// BRDF), ported line-by-line from EasyGLRenderer.cpp's EnsurePbrProgram() (GLSL/GLES 3.00)
// to HLSL Shader Model 3 (vs_3_0/ps_3_0). Not a Microsoft Stock Effect port: XNA 4.0 has no PBR
// effect at all -- same "CNA's own minimal stand-in, real XNA has nothing like this" rationale
// D3D9InstancedDraw.cpp's own header comment already gives for Instanced3D.hlsl.
//
// Targets vs_3_0/ps_3_0, NOT vs_2_0/ps_2_0 like the vendored XNA Stock Effects: empirically
// confirmed against this project's own real d3dcompiler_47.dll (the same DLL D9-71's stock-effect
// pipeline uses, under Wine) -- compiling this file's pixel shader at /T ps_2_0 fails with
// "error X4505: maximum temp register index exceeded" (ps_2_0's 12-temp-register file is too small
// for the live values PbrLight()'s BRDF math, TBN reconstruction, and 4 texture samples need to
// keep simultaneously live -- a real, compiler-enforced ps_2_0 hardware limit, not a guess). ps_3_0
// has a far larger temp-register file (32, matching its own much higher instruction budget too) and
// compiles this file cleanly with no simplification needed -- see this task's own final report for
// the full compiler output.
//
// Register layout is fully self-chosen (mirrors Instanced3D.hlsl's own precedent: this shader has
// no Microsoft original to extract a CTAB register table from) -- verified against a real
// D3DDisassemble() (disasm_tool.exe, same tool D9-72 used for the vendored Stock Effects), not
// hand-assembled or guessed.
//
// Vertex declaration (stride 48, D3D9VertexDeclarations.hpp): POSITION0 (FLOAT3, 0), NORMAL0
// (FLOAT3, 12), TANGENT0 (FLOAT4, 24 -- xyz=tangent, w=bitangent sign, glTF convention), TEXCOORD0
// (FLOAT2, 40). Texture samplers: s0=base color (Texture), s1=NormalMap, s2=MetallicRoughnessMap
// (glTF packing: G=roughness, B=metallic), s3=EmissiveMap, s4=OcclusionMap (R channel) -- matches
// EnsurePbrProgram()'s own texture-unit assignment (0..4) and GpuDrawParams' own field order.

float4x4 WorldViewProj : register(c0); // c0-c3
float4x4 World         : register(c4); // c4-c7
float3x3 NormalMatrix  : register(c8); // c8-c10 (WorldInverseTranspose, upper 3x3)
float4   FogParams     : register(c11); // REMED-GFX-010: FNA view-space fog vector (was FogEnabled/Start/End)

struct VSInput
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Tangent  : TANGENT0;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1; // xyz=tangent, w=bitangent sign
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
};

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

VSOutput VSPbr3D(VSInput vin)
{
    VSOutput vout;

    vout.Position = mul(float4(vin.Position, 1.0), WorldViewProj);
    vout.Normal = mul(vin.Normal, NormalMatrix);
    // Tangent transforms as a plain direction under (float3x3)World (not the inverse-transpose
    // NormalMatrix) -- correct for uniform-scale World transforms, matching
    // EnsurePbrProgram()'s own identical documented simplification.
    float3x3 worldDirectionMat = (float3x3)World;
    vout.TangentWS = float4(mul(vin.Tangent.xyz, worldDirectionMat),
                            vin.Tangent.w * CnaDirectionHandedness(worldDirectionMat));
    vout.UV = vin.UV;
    vout.WorldPos = mul(float4(vin.Position, 1.0), World).xyz;
    // REMED-GFX-010: FNA view-space fog. FogParams now carries EffectHelpers.SetFogVector
    // (World*View 3rd column); keep = 1 - saturate(dot(objectPos, fogVector)) is true eye-space-Z
    // fog, not object-space. Zero vector (fog disabled) -> dot 0 -> keep 1 (no fog).
    vout.FogFactor = 1.0 - saturate(dot(float4(vin.Position, 1.0), FogParams));

    return vout;
}

sampler2D Texture              : register(s0);
sampler2D NormalMap            : register(s1);
sampler2D MetallicRoughnessMap : register(s2);
sampler2D EmissiveMap          : register(s3);
sampler2D OcclusionMap         : register(s4);

float4 DiffuseColor           : register(c0);
float3 AmbientColor            : register(c1);
float3 EmissiveColor           : register(c2);
float4 MetallicRoughnessFactor : register(c3); // x=metallic, y=roughness, z=normal scale, w=occlusion strength
float3 Light0Dir               : register(c4);
float3 Light0Diffuse           : register(c5);
float3 Light1Dir               : register(c6);
float3 Light1Diffuse           : register(c7);
float3 Light2Dir               : register(c8);
float3 Light2Diffuse           : register(c9);
float3 EyePosition             : register(c10);
float4 AlphaTest               : register(c11);
float3 FogColor                : register(c12);

struct PSInput
{
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
};

// GGX/Trowbridge-Reitz normal distribution (D), Smith-Schlick-GGX geometry/visibility term
// (direct-lighting k=(roughness+1)^2/8), and Schlick Fresnel (F) -- ported line-by-line from
// EnsurePbrProgram()'s own PbrLight() (the glTF 2.0 spec's own reference BRDF, Appendix
// B.3.3/B.3.4/B.3.2).
float3 PbrLight(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0,
                float roughness, float metallic)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a2 = pow(roughness, 4.0);
    float dTerm = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    float D = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    float k = (roughness + 1.0);
    k = k * k / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    float3 F = F0 + (float3(1.0, 1.0, 1.0) - F0) * pow(saturate(1.0 - VdotH), 5.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0 - metallic);
    float3 kd = float3(1.0, 1.0, 1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

float4 PSPbr3D(PSInput pin) : SV_Target0
{
    float4 baseColorTex = tex2D(Texture, pin.UV);
    float3 albedo = baseColorTex.rgb * DiffuseColor.rgb;
    float alpha = baseColorTex.a * DiffuseColor.a;

    float3 N = normalize(pin.Normal);
    float3 T = normalize(pin.TangentWS.xyz - N * dot(N, pin.TangentWS.xyz));
    float3 B = cross(N, T) * pin.TangentWS.w;
    float3x3 TBN = float3x3(T, B, N);

    float3 sampledNormal = tex2D(NormalMap, pin.UV).rgb * 2.0 - 1.0;
    sampledNormal.xy *= MetallicRoughnessFactor.z;
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    float4 mr = tex2D(MetallicRoughnessMap, pin.UV);
    float roughness = clamp(mr.g * MetallicRoughnessFactor.y, 0.045, 1.0);
    float metallic = clamp(mr.b * MetallicRoughnessFactor.x, 0.0, 1.0);

    float3 V = normalize(EyePosition - pin.WorldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    Lo += PbrLight(finalNormal, V, normalize(-Light0Dir), Light0Diffuse, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light1Dir), Light1Diffuse, albedo, F0, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light2Dir), Light2Diffuse, albedo, F0, roughness, metallic);

    float occlusion = tex2D(OcclusionMap, pin.UV).r;
    occlusion = 1.0 + MetallicRoughnessFactor.w * (occlusion - 1.0);
    float3 ambient = AmbientColor * albedo * occlusion;
    float3 emissive = EmissiveColor * tex2D(EmissiveMap, pin.UV).rgb;

    float4 outColor = float4(ambient + Lo + emissive, alpha);

    float alphaTestResult = (AlphaTest.y > 0.0)
        ? ((abs(outColor.a - AlphaTest.x) < AlphaTest.y) ? AlphaTest.z : AlphaTest.w)
        : ((outColor.a < AlphaTest.x) ? AlphaTest.z : AlphaTest.w);
    clip(alphaTestResult);

    outColor.rgb = lerp(FogColor, outColor.rgb, pin.FogFactor);
    return outColor;
}
