// SPDX-License-Identifier: MS-PL
// D3D9 PBR porting task: CNA's own CNAEXT "PbrSkinned3D" shader -- SkinnedPbrEffect (same
// metallic-roughness BRDF as Pbr3D.hlsl, plus bone skinning), ported line-by-line from
// EasyGLRenderer.cpp's EnsurePbrSkinnedProgram() (GLSL/GLES 3.00) to HLSL Shader Model 3
// (vs_3_0/ps_3_0). See Pbr3D.hlsl's own header comment for why this is a CNA custom shader (no
// Microsoft Stock Effect equivalent) and why SM3 (not SM2) is the target -- both reasons apply
// identically here; this file's own pixel shader is a straight copy of Pbr3D.hlsl's (same BRDF,
// same PSInput shape), so the same empirically-confirmed "ps_2_0 fails with X4505 maximum temp
// register index exceeded, ps_3_0 compiles cleanly" finding applies without re-deriving it.
//
// Bone skinning mirrors the vendored, real Microsoft SkinnedEffect.fx's own Skin() function
// exactly (src/CNA/Internal/Renderers/DirectX9/shaders/xna/SkinnedEffect.fx): a fixed-size
// float4x3 Bones[72] array (3 registers/bone, the same ColumnCount==4/RowCount==3
// EffectParameter.SetValue(Matrix) packing D3D9EffectDraw.cpp's own UploadBonesVS() already
// uses for the real SkinnedEffect), accumulated per-vertex via BLENDINDICES0/BLENDWEIGHT0, and the
// same "(float3x3)skinning" cast to extract the rotation-only part for the normal/tangent -- proven
// idiom, not invented (SkinnedEffect.fx's own Skin() does `vin.Normal = mul(vin.Normal,
// (float3x3)skinning);`).
//
// Deviation from EnsurePbrSkinnedProgram()'s own GLSL (documented, not silent): after the
// skin-local rotation, the skinned Normal/Tangent are further transformed by (float3x3)World
// before use -- EasyGL's own EnsurePbrSkinnedProgram() does this too
// (`vNormal=normalize(mat3(uWorld)*(skinNormalMat*aNormal));`), so this is a faithful port, not a
// deviation from THAT function. (EnsureSkinnedProgram()'s separate, non-PBR skinned path omits this
// World step for its own Normal -- SkinnedVertexColor3D.hlsl's own header comment discusses that
// inconsistency; it does not apply here since EnsurePbrSkinnedProgram() already includes the step.)
//
// Vertex declaration (stride 68, D3D9VertexDeclarations.hpp): POSITION0 (FLOAT3, 0), NORMAL0
// (FLOAT3, 12), TANGENT0 (FLOAT4, 24), TEXCOORD0 (FLOAT2, 40), BLENDWEIGHT0 (FLOAT4, 48),
// BLENDINDICES0 (UBYTE4, 64). Texture sampler assignment identical to Pbr3D.hlsl.
//
// Register layout: VS constants WorldViewProj(c0-c3)/World(c4-c7)/FogParams(c8, w=WeightsPerVertex)
// leave Bones[72] starting at c9 (3*72=216 registers, c9..c224) -- 225 registers total, within
// vs_3_0's guaranteed 256-register minimum (matches real SkinnedEffect.fx's own proven usage up to
// c241, same order of magnitude). PS constants identical to Pbr3D.hlsl's own layout (register-
// verified against a real D3DDisassemble(), not guessed -- see this task's final report).

#define PBR_SKINNED_MAX_BONES 72

float4x4 WorldViewProj                : register(c0); // c0-c3
float4x4 World                         : register(c4); // c4-c7
float4   FogParams                     : register(c8); // REMED-GFX-010: only .w = WeightsPerVertex now (xyz unused)
float4x3 Bones[PBR_SKINNED_MAX_BONES]  : register(c9); // c9..c224 (216 registers)
float4   FogVector                     : register(c225); // REMED-GFX-010: FNA view-space fog vector (SetFogVector)

struct VSInput
{
    float3 Position    : POSITION0;
    float3 Normal      : NORMAL0;
    float4 Tangent     : TANGENT0;
    float2 UV          : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4   BoneIndices : BLENDINDICES0;
};

// plans/plan_gltf.md GLTF-465/GLTF-463: the stride-80 twin. Stride 80 is the stride-76 skinned PBR
// record with a packed, normalized COLOR_0 appended at offset 76. A separate input struct for the
// same reason as the rigid pair: the stride-68/76 declarations have no colour element.
struct VSInputColor
{
    float3 Position    : POSITION0;
    float3 Normal      : NORMAL0;
    float4 Tangent     : TANGENT0;
    float2 UV          : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4   BoneIndices : BLENDINDICES0;
    float4 Color       : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float4 TangentWS : TEXCOORD1;
    float2 UV        : TEXCOORD2;
    float3 WorldPos  : TEXCOORD3;
    float  FogFactor : TEXCOORD4;
    // Written by both entry points: the authored colour, or opaque white where the layout has none.
    float4 Color     : COLOR0;
};

// REMED-GFX-006: transpose(inverse(m)) of a 3x3 (cofactor matrix over its determinant). HLSL has
// no built-in inverse(); vs_3_0 has ample instruction budget for this. Matches Pbr3D.hlsl's own
// CPU-supplied NormalMatrix and the corrected Vulkan pbr3d_skinned.vert.glsl.
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

float3 CnaSkinNormal(float3x3 m, float3 n)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    float3 transformed = mul(n, float3x3(c0, c1, c2));
    return abs(det) > 1e-6 ? transformed * sign(det) : mul(n, m);
}

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

VSOutput VSPbrSkinned3DBody(VSInput vin, float4 color)
{
    VSOutput vout;
    vout.Color = color;

    // Task 895's real Skin(vin, boneCount) shape: only sum the first WeightsPerVertex (1, 2, or 4)
    // weight/index pairs, matching XNA's own validated SkinningEffect.WeightsPerVertex domain.
    float weightsPerVertex = FogParams.w;
    float4x3 skinning = Bones[vin.BoneIndices.x] * vin.BoneWeights.x;
    if (weightsPerVertex >= 2.0)
        skinning += Bones[vin.BoneIndices.y] * vin.BoneWeights.y;
    if (weightsPerVertex >= 4.0)
        skinning += Bones[vin.BoneIndices.z] * vin.BoneWeights.z
                   + Bones[vin.BoneIndices.w] * vin.BoneWeights.w;

    float3 skinnedPos = mul(float4(vin.Position, 1.0), skinning);
    float3x3 skinNormalMat = (float3x3)skinning;
    float3 skinnedNormal = CnaSkinNormal(skinNormalMat, vin.Normal);
    float3 skinnedTangent = mul(vin.Tangent.xyz, skinNormalMat);

    vout.Position = mul(float4(skinnedPos, 1.0), WorldViewProj);
    // GLTF-264/REMED-GFX-006: inverse-transpose both the blended joint matrix and World. Tangent
    // stays on their raw direction matrices.
    float3x3 worldDirectionMat = (float3x3)World;
    vout.Normal = normalize(mul(skinnedNormal, InverseTranspose3x3(worldDirectionMat)));
    vout.TangentWS = float4(mul(skinnedTangent, worldDirectionMat),
                            vin.Tangent.w * CnaDirectionHandedness(worldDirectionMat)
                                * CnaDirectionHandedness(skinNormalMat));
    vout.UV = vin.UV;
    vout.WorldPos = mul(float4(skinnedPos, 1.0), World).xyz;
    // REMED-GFX-010: FNA view-space fog on the POST-skin position (FNA Skin() mutates vin.Position
    // before ComputeFogFactor). keep = 1 - saturate(dot(skinPos, fogVector)); zero vector = no fog.
    vout.FogFactor = 1.0 - saturate(dot(float4(skinnedPos, 1.0), FogVector));

    return vout;
}

VSOutput VSPbrSkinned3D(VSInput vin)
{
    // Stride 68/76 carry no colour element: opaque white is the multiplier's identity.
    return VSPbrSkinned3DBody(vin, float4(1.0, 1.0, 1.0, 1.0));
}

VSOutput VSPbrSkinned3DColor(VSInputColor vin)
{
    VSInput bare;
    bare.Position = vin.Position;
    bare.Normal = vin.Normal;
    bare.Tangent = vin.Tangent;
    bare.UV = vin.UV;
    bare.BoneWeights = vin.BoneWeights;
    bare.BoneIndices = vin.BoneIndices;
    return VSPbrSkinned3DBody(bare, vin.Color);
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
// plans/plan_gltf.md GLTF-465: same free register, same meaning, as Pbr3D.hlsl's own.
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

// Identical BRDF to Pbr3D.hlsl's own PbrLight() -- see that file's own comment for the citation.
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

float4 PSPbrSkinned3D(PSInput pin) : SV_Target0
{
    float4 baseColorTex = tex2D(Texture, CnaPbrTransformUv(pin.UV, 0));
    float3 baseColor = lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb), AmbientColor.w);
    float3 albedo = baseColor * DiffuseColor.rgb;
    float alpha = baseColorTex.a * DiffuseColor.a;
    // glTF 3.9.2: COLOR_0 multiplies the whole base-colour product, alpha included, before the
    // alpha test below consumes that alpha.
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
