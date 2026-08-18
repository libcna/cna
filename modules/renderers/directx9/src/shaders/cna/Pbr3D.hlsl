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
// (glTF packing: G=roughness, B=metallic), s3=EmissiveMap, s4=OcclusionMap (R channel),
// s5=specular strength and s6=specular colour.

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

// plan_gltf.md GLTF-465: the stride-60 twin of VSInput. glTF 3.9.2 makes COLOR_0 an additional
// linear multiplier on base colour, and stride 60 is the rigid PBR record that carries it (offset
// 56, packed as a normalized D3DCOLOR). It is a SEPARATE input struct rather than an optional field
// because the stride-48 declaration has no colour element, and a vs_3_0 input D3D9 has no stream for
// reads undefined -- so the two layouts get two entry points sharing one body.
struct VSInputColor
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Tangent  : TANGENT0;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1; // xyz=tangent, w=bitangent sign
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
    // Written by BOTH entry points: the authored colour where the layout supplies one, opaque white
    // -- the multiplier's identity -- where it does not, so one pixel shader serves both.
    float4 Color     : COLOR0;
};

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

VSOutput VSPbr3DBody(float3 position, float3 normal, float4 tangent, float2 uv, float4 color)
{
    VSOutput vout;
    VSInput vin;
    vin.Position = position;
    vin.Normal = normal;
    vin.Tangent = tangent;
    vin.UV = uv;
    vout.Color = color;

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

VSOutput VSPbr3D(VSInput vin)
{
    // No colour element in the stride-48 declaration: opaque white is the multiplier's identity, so
    // the shared pixel stage multiplies by one instead of reading a stream that is not there.
    return VSPbr3DBody(vin.Position, vin.Normal, vin.Tangent, vin.UV, float4(1.0, 1.0, 1.0, 1.0));
}

VSOutput VSPbr3DColor(VSInputColor vin)
{
    return VSPbr3DBody(vin.Position, vin.Normal, vin.Tangent, vin.UV, vin.Color);
}

sampler2D Texture              : register(s0);
sampler2D NormalMap            : register(s1);
sampler2D MetallicRoughnessMap : register(s2);
sampler2D EmissiveMap          : register(s3);
sampler2D OcclusionMap         : register(s4);
sampler2D SpecularMap          : register(s5);
sampler2D SpecularColorMap     : register(s6);

float4 DiffuseColor           : register(c0);
float4 AmbientColor            : register(c1); // xyz=color, w=decode base-color texture from sRGB
float4 EmissiveColor           : register(c2); // xyz=color, w=decode emissive texture from sRGB
float4 MetallicRoughnessFactor : register(c3); // x=metallic, y=roughness, z=normal scale, w=occlusion strength
float3 Light0Dir               : register(c4);
float3 Light0Diffuse           : register(c5);
float3 Light1Dir               : register(c6);
float3 Light1Diffuse           : register(c7);
float3 Light2Dir               : register(c8);
float3 Light2Diffuse           : register(c9);
float3 EyePosition             : register(c10);
float4 AlphaTest               : register(c11);
float4 FogColor                : register(c12); // xyz=color, w=encode PBR output to sRGB
// plan_gltf.md GLTF-465: c13 was the one free register between FogColor and TextureTransformRows.
// x is the effect's own VertexColorEnabledEXT, so an application can opt back into the opaque-white
// identity on coloured geometry deliberately.
float4 VertexColorFlags        : register(c13); // x = VertexColorEnabledEXT
float4 TextureTransformRows[10] : register(c14); // two affine UV rows per PBR map
float4 SpecularFresnelInputs    : register(c24); // xyz=unclamped F0, w=specular factor
float4 SpecularMapFlags        : register(c25); // x=decode specular-colour sample from sRGB
float4 SpecularTextureTransformRows[4] : register(c26); // two affine rows per specular map

struct PSInput
{
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
    float4 Color     : COLOR0;
};

float3 CnaSrgbToLinear(float3 color)
{
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return lerp(low, high, step(float3(0.04045, 0.04045, 0.04045), color));
}

float3 CnaLinearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float exponent = 1.0 / 2.4;
    float3 high = 1.055 * pow(max(color, 0.0), float3(exponent, exponent, exponent)) - 0.055;
    return lerp(low, high, step(float3(0.0031308, 0.0031308, 0.0031308), color));
}

float2 CnaPbrTransformUv(float2 uv, int slot)
{
    float3 value = float3(uv, 1.0);
    return float2(dot(value, TextureTransformRows[slot * 2].xyz),
                  dot(value, TextureTransformRows[slot * 2 + 1].xyz));
}

float2 CnaPbrSpecularTransformUv(float2 uv, int slot)
{
    float3 value = float3(uv, 1.0);
    return float2(dot(value, SpecularTextureTransformRows[slot * 2].xyz),
                  dot(value, SpecularTextureTransformRows[slot * 2 + 1].xyz));
}

// GGX/Trowbridge-Reitz normal distribution (D), Smith-Schlick-GGX geometry/visibility term
// (direct-lighting k=(roughness+1)^2/8), and Schlick Fresnel (F) -- ported line-by-line from
// EnsurePbrProgram()'s own PbrLight() (the glTF 2.0 spec's own reference BRDF, Appendix
// B.3.3/B.3.4/B.3.2).
float3 PbrLight(float3 N, float3 V, float3 L, float3 lightColor, float3 albedo, float3 F0,
                float3 F90, float roughness, float metallic)
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
    float3 F = F0 + (F90 - F0) * pow(saturate(1.0 - VdotH), 5.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0 - metallic);
    float3 kd = float3(1.0, 1.0, 1.0) - F;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * NdotL;
}

float4 PSPbr3D(PSInput pin) : SV_Target0
{
    float4 baseColorTex = tex2D(Texture, CnaPbrTransformUv(pin.UV, 0));
    float3 baseColor = lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), AmbientColor.w);
    float3 albedo = baseColor * DiffuseColor.rgb;
    float alpha = baseColorTex.a * DiffuseColor.a;
    // glTF 3.9.2: COLOR_0 is an additional LINEAR multiplier on the whole base-colour product, its
    // alpha included -- the alpha half is where a BLEND-mode vertex-coloured primitive's
    // transparency comes from. Before the alpha test below, which consumes that alpha.
    if (VertexColorFlags.x > 0.5)
    {
        albedo *= pin.Color.rgb;
        alpha  *= pin.Color.a;
    }

    float3 N = normalize(pin.Normal);
    float3 T = normalize(pin.TangentWS.xyz - N * dot(N, pin.TangentWS.xyz));
    float3 B = cross(N, T) * pin.TangentWS.w;
    float3x3 TBN = float3x3(T, B, N);

    float3 sampledNormal = tex2D(NormalMap, CnaPbrTransformUv(pin.UV, 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= MetallicRoughnessFactor.z;
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    float4 mr = tex2D(MetallicRoughnessMap, CnaPbrTransformUv(pin.UV, 2));
    float roughness = clamp(mr.g * MetallicRoughnessFactor.y, 0.045, 1.0);
    float metallic = clamp(mr.b * MetallicRoughnessFactor.x, 0.0, 1.0);

    float3 V = normalize(EyePosition - pin.WorldPos);
    float specularWeight = SpecularFresnelInputs.w
        * tex2D(SpecularMap, CnaPbrSpecularTransformUv(pin.UV, 0)).a;
    float3 specularColorTex = tex2D(
        SpecularColorMap, CnaPbrSpecularTransformUv(pin.UV, 1)).rgb;
    specularColorTex = lerp(
        specularColorTex, CnaSrgbToLinear(specularColorTex), SpecularMapFlags.x);
    float3 dielectricF0 = min(SpecularFresnelInputs.xyz * specularColorTex, 1.0)
        * specularWeight;
    float3 F0 = lerp(dielectricF0, albedo, metallic);
    float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight),
                      float3(1.0, 1.0, 1.0), metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    Lo += PbrLight(finalNormal, V, normalize(-Light0Dir), Light0Diffuse, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light1Dir), Light1Diffuse, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-Light2Dir), Light2Diffuse, albedo, F0, F90, roughness, metallic);

    float occlusion = tex2D(OcclusionMap, CnaPbrTransformUv(pin.UV, 4)).r;
    occlusion = 1.0 + MetallicRoughnessFactor.w * (occlusion - 1.0);
    float3 ambient = AmbientColor.xyz * albedo * occlusion;
    float3 emissiveSample = tex2D(EmissiveMap, CnaPbrTransformUv(pin.UV, 3)).rgb;
    emissiveSample = lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), EmissiveColor.w);
    float3 emissive = EmissiveColor.xyz * emissiveSample;

    float4 outColor = float4(ambient + Lo + emissive, alpha);

    float alphaTestResult = (AlphaTest.y > 0.0)
        ? ((abs(outColor.a - AlphaTest.x) < AlphaTest.y) ? AlphaTest.z : AlphaTest.w)
        : ((outColor.a < AlphaTest.x) ? AlphaTest.z : AlphaTest.w);
    clip(alphaTestResult);

    float3 fogLinear = lerp(FogColor.xyz, CnaSrgbToLinear(FogColor.xyz), FogColor.w);
    outColor.rgb = lerp(fogLinear, outColor.rgb, pin.FogFactor);
    outColor.rgb = lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w);
    return outColor;
}
