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
float4   FogParams     : register(c11); // FNA view-space fog vector (was FogEnabled/Start/End)

struct VSInput
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Tangent  : TANGENT0;
    float2 UV       : TEXCOORD0;
};

// plans/plan_gltf.md GLTF-465: the stride-60 twin of VSInput. glTF 3.9.2 makes COLOR_0 an additional
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

#if CNA_D3D9_DUAL_UV
struct VSInputDualUv
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Tangent  : TANGENT0;
    float2 UV       : TEXCOORD0;
    float2 UV1      : TEXCOORD1;
};

struct VSInputDualUvColor
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float4 Tangent  : TANGENT0;
    float2 UV       : TEXCOORD0;
    float2 UV1      : TEXCOORD1;
    float4 Color    : COLOR0;
};
#endif

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1; // xyz=tangent, w=bitangent sign
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
#if CNA_D3D9_DUAL_UV
    float2 UV1       : TEXCOORD5;
#endif
    // Written by BOTH entry points: the authored colour where the layout supplies one, opaque white
    // -- the multiplier's identity -- where it does not, so one pixel shader serves both.
    float4 Color     : COLOR0;
};

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

VSOutput VSPbr3DBody(float3 position, float3 normal, float4 tangent, float2 uv,
                     float2 uv1, float4 color)
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
#if CNA_D3D9_DUAL_UV
    vout.UV1 = uv1;
#endif
    vout.WorldPos = mul(float4(vin.Position, 1.0), World).xyz;
    // FNA view-space fog. FogParams now carries EffectHelpers.SetFogVector
    // (World*View 3rd column); keep = 1 - saturate(dot(objectPos, fogVector)) is true eye-space-Z
    // fog, not object-space. Zero vector (fog disabled) -> dot 0 -> keep 1 (no fog).
    vout.FogFactor = 1.0 - saturate(dot(float4(vin.Position, 1.0), FogParams));

    return vout;
}

VSOutput VSPbr3D(VSInput vin)
{
    // No colour element in the stride-48 declaration: opaque white is the multiplier's identity, so
    // the shared pixel stage multiplies by one instead of reading a stream that is not there.
    return VSPbr3DBody(vin.Position, vin.Normal, vin.Tangent, vin.UV, vin.UV,
                       float4(1.0, 1.0, 1.0, 1.0));
}

VSOutput VSPbr3DColor(VSInputColor vin)
{
    return VSPbr3DBody(vin.Position, vin.Normal, vin.Tangent, vin.UV, vin.UV, vin.Color);
}

#if CNA_D3D9_DUAL_UV
VSOutput VSPbr3DDualUv(VSInputDualUv vin)
{
    return VSPbr3DBody(vin.Position, vin.Normal, vin.Tangent, vin.UV, vin.UV1,
                       float4(1.0, 1.0, 1.0, 1.0));
}

VSOutput VSPbr3DDualUvColor(VSInputDualUvColor vin)
{
    return VSPbr3DBody(vin.Position, vin.Normal, vin.Tangent, vin.UV, vin.UV1, vin.Color);
}
#endif

sampler2D Texture              : register(s0);
sampler2D NormalMap            : register(s1);
sampler2D MetallicRoughnessMap : register(s2);
sampler2D EmissiveMap          : register(s3);
sampler2D OcclusionMap         : register(s4);
sampler2D SpecularMap          : register(s5);
sampler2D SpecularColorMap     : register(s6);
#if CNA_D3D9_IBL
// FNA3D is CNA's Effect-Framework consumer of this otherwise Direct3D-9 shader. Keep the
// environment on the public slots shared by EasyGL/Software/WebGPU: irradiance=10,
// prefiltered-specular=11, split-sum LUT=12. DirectX9 leaves CNA_D3D9_IBL undefined, so its
// compiled shaders and sampler budget are unchanged.
samplerCUBE IblIrradiance       : register(s10);
samplerCUBE IblSpecular         : register(s11);
sampler2D   IblBrdfLut          : register(s12);
#endif

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
// plans/plan_gltf.md GLTF-465: c13 was the one free register between FogColor and TextureTransformRows.
// x is the effect's own VertexColorEnabledEXT, so an application can opt back into the opaque-white
// identity on coloured geometry deliberately.
float4 VertexColorFlags        : register(c13); // x = VertexColorEnabledEXT
float4 TextureTransformRows[10] : register(c14); // two affine UV rows per PBR map
float4 SpecularFresnelInputs    : register(c24); // xyz=unclamped F0, w=specular factor
float4 SpecularMapFlags        : register(c25); // x=decode specular-colour sample from sRGB
float4 SpecularTextureTransformRows[4] : register(c26); // two affine rows per specular map
float4 PunctualPositionRange   : register(c30); // xyz=world position, w=range
float4 PunctualDirectionKind   : register(c31); // xyz=spot direction, w=0 none / 1 point / 2 spot
float4 PunctualDiffuse         : register(c32); // xyz=linear colour/intensity
float4 PunctualCone            : register(c33); // x=cos(inner), y=cos(outer)
#if CNA_D3D9_IBL
// Kept above the shadow block's c34-c75 range so FNA3D can compose both extensions later.
float4 IblParams               : register(c76); // x=enabled, y=mip count, z=intensity
#endif
#if CNA_D3D9_DOUBLE_SIDED
float4 PbrDoubleSidedParams    : register(c77); // x=0 single-sided, +/-1 world orientation
#endif
#if CNA_D3D9_DUAL_UV
float4 PbrTextureCoordinateSets  : register(c78); // base, normal, metallic-roughness, emissive
float4 PbrTextureCoordinateSets2 : register(c79); // occlusion, specular, specular-colour
#endif

#if CNA_D3D9_SHADOWS
#include "ShadowSampling3D.fxh"
#else
float CnaShadowFactor(float3 worldPos) { return 1.0; }
float3 CnaCascadeDebugTint(float3 worldPos) { return float3(1.0, 1.0, 1.0); }
float CnaPunctualShadow(float3 worldPos, float3 toLight, float distanceToLight) { return 1.0; }
#endif

struct PSInput
{
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
#if CNA_D3D9_DUAL_UV
    float2 UV1       : TEXCOORD5;
#endif
    float4 Color     : COLOR0;
};

float2 CnaPbrUv(PSInput pin, int slot)
{
#if CNA_D3D9_DUAL_UV
    float setIndex = slot == 0 ? PbrTextureCoordinateSets.x
        : slot == 1 ? PbrTextureCoordinateSets.y
        : slot == 2 ? PbrTextureCoordinateSets.z
        : slot == 3 ? PbrTextureCoordinateSets.w
        : slot == 4 ? PbrTextureCoordinateSets2.x
        : slot == 5 ? PbrTextureCoordinateSets2.y
                    : PbrTextureCoordinateSets2.z;
    return setIndex > 0.5 ? pin.UV1 : pin.UV;
#else
    return pin.UV;
#endif
}

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

// MOD-1005: same unshadowed point/spot term as the other PBR backends. Shadow textures remain
// deliberately unsupported by DIRECTX9 and therefore do not affect this direct-light result.
float3 CnaPunctualLight(float3 worldPos, float3 normal)
{
    if (PunctualDirectionKind.w < 0.5)
        return float3(0.0, 0.0, 0.0);
    float3 offset = PunctualPositionRange.xyz - worldPos;
    float distanceToLight = length(offset);
    if (distanceToLight > PunctualPositionRange.w || distanceToLight < 1e-5)
        return float3(0.0, 0.0, 0.0);
    float3 toLight = offset / distanceToLight;
    float t = distanceToLight / PunctualPositionRange.w;
    float window = saturate(1.0 - t * t * t * t);
    float attenuation = window * window / (1.0 + distanceToLight * distanceToLight);
    if (PunctualDirectionKind.w > 1.5)
    {
        float cosAngle = dot(normalize(PunctualDirectionKind.xyz), -toLight);
        float cone = saturate((cosAngle - PunctualCone.y) /
                              max(PunctualCone.x - PunctualCone.y, 1e-4));
        attenuation *= cone * cone;
    }
    float ndotl = max(dot(normal, toLight), 0.0);
    return PunctualDiffuse.xyz * ndotl * attenuation *
           CnaPunctualShadow(worldPos, toLight, distanceToLight);
}

#if CNA_D3D9_IBL
// Same split-sum equation used by EasyGL, Software and WebGPU. texCUBElod makes the authored
// roughness-to-mip rule explicit and lets sampler slot 11 choose point versus linear mip blending.
float3 CnaIblAmbient(float3 N, float3 V, float3 albedo, float3 F0, float roughness,
                     float metallic, float occlusion)
{
    if (IblParams.x < 0.5)
        return float3(0.0, 0.0, 0.0);
    float NdotV = clamp(dot(N, V), 1e-4, 1.0);
    float3 kS = F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0)
        * pow(1.0 - NdotV, 5.0);
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
    float3 diffuse = texCUBElod(IblIrradiance, float4(N, 0.0)).rgb * albedo * kD;
    float3 R = reflect(-V, N);
    float lod = roughness * max(IblParams.y - 1.0, 0.0);
    float3 prefiltered = texCUBElod(IblSpecular, float4(R, lod)).rgb;
    float2 ab = tex2Dlod(IblBrdfLut, float4(NdotV, roughness, 0.0, 0.0)).rg;
    float3 specular = prefiltered * (kS * ab.x + ab.y);
    return (diffuse + specular) * IblParams.z * occlusion;
}
#else
float3 CnaIblAmbient(float3 N, float3 V, float3 albedo, float3 F0, float roughness,
                     float metallic, float occlusion)
{
    return float3(0.0, 0.0, 0.0);
}
#endif

#if CNA_D3D9_DOUBLE_SIDED
float4 PSPbr3D(PSInput pin, float face : VFACE) : SV_Target0
#else
float4 PSPbr3D(PSInput pin) : SV_Target0
#endif
{
    float4 baseColorTex = tex2D(Texture, CnaPbrTransformUv(CnaPbrUv(pin, 0), 0));
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
#if CNA_D3D9_DOUBLE_SIDED
    // D3D9 VFACE is positive for the native front face. A mirroring World reverses the
    // rasterized winding, so its determinant sign reverses which VFACE needs the basis flip.
    if (PbrDoubleSidedParams.x != 0.0 && face * PbrDoubleSidedParams.x < 0.0)
    {
        N = -N;
        T = -T;
        B = -B;
    }
#endif
    float3x3 TBN = float3x3(T, B, N);

    float3 sampledNormal = tex2D(
        NormalMap, CnaPbrTransformUv(CnaPbrUv(pin, 1), 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= MetallicRoughnessFactor.z;
    float3 finalNormal = normalize(mul(sampledNormal, TBN));

    float4 mr = tex2D(
        MetallicRoughnessMap, CnaPbrTransformUv(CnaPbrUv(pin, 2), 2));
    float roughness = clamp(mr.g * MetallicRoughnessFactor.y, 0.045, 1.0);
    float metallic = clamp(mr.b * MetallicRoughnessFactor.x, 0.0, 1.0);

    float3 V = normalize(EyePosition - pin.WorldPos);
    float specularWeight = SpecularFresnelInputs.w
        * tex2D(SpecularMap, CnaPbrSpecularTransformUv(CnaPbrUv(pin, 5), 0)).a;
    float3 specularColorTex = tex2D(
        SpecularColorMap, CnaPbrSpecularTransformUv(CnaPbrUv(pin, 6), 1)).rgb;
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
    Lo *= CnaShadowFactor(pin.WorldPos);
    Lo += CnaPunctualLight(pin.WorldPos, finalNormal) * albedo;

    float occlusion = tex2D(
        OcclusionMap, CnaPbrTransformUv(CnaPbrUv(pin, 4), 4)).r;
    occlusion = 1.0 + MetallicRoughnessFactor.w * (occlusion - 1.0);
    float3 ambient = AmbientColor.xyz * albedo * occlusion
        + CnaIblAmbient(finalNormal, V, albedo, F0, roughness, metallic, occlusion);
    float3 emissiveSample = tex2D(
        EmissiveMap, CnaPbrTransformUv(CnaPbrUv(pin, 3), 3)).rgb;
    emissiveSample = lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), EmissiveColor.w);
    float3 emissive = EmissiveColor.xyz * emissiveSample;

    float4 outColor = float4(ambient + Lo + emissive, alpha);
    outColor.rgb *= CnaCascadeDebugTint(pin.WorldPos);

    float alphaTestResult = (AlphaTest.y > 0.0)
        ? ((abs(outColor.a - AlphaTest.x) < AlphaTest.y) ? AlphaTest.z : AlphaTest.w)
        : ((outColor.a < AlphaTest.x) ? AlphaTest.z : AlphaTest.w);
    clip(alphaTestResult);

    float3 fogLinear = lerp(FogColor.xyz, CnaSrgbToLinear(FogColor.xyz), FogColor.w);
    outColor.rgb = lerp(fogLinear, outColor.rgb, pin.FogFactor);
    outColor.rgb = lerp(outColor.rgb, CnaLinearToSrgb(outColor.rgb), FogColor.w);
    return outColor;
}
