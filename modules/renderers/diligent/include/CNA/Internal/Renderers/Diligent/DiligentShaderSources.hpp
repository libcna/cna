// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-68: every built-in shader's HLSL source, split out of
// DiligentRenderer.cpp so a shader/lighting change is reviewable on its own instead of
// buried in a 4700-line implementation file. Diligent compiles HLSL at runtime on every device
// type (SPIR-V via glslang on Vulkan, GLSL via HLSL2GLSL on OpenGL, DXBC/DXIL on Direct3D), so
// these remain plain C++ string literals rather than separate .hlsl files needing a build-time
// embed step -- included by DiligentRenderer.cpp only, no behavior change from moving them.
#pragma once

namespace CNA::Internal::Renderers::Diligent
{
    namespace
    {
        /// Every built-in shader shares one constant buffer, so one HLSL declaration is prepended
        /// to each program instead of being repeated. The matrices are row-major deliberately: CNA's
        /// Matrix stores rows contiguously and XNA's convention is `v * M`, so this lets the shaders
        /// write `mul(v, m)` with the CNA matrix memory uploaded verbatim. `#pragma pack_matrix` is
        /// used instead of an inline `row_major` qualifier on each variable: Diligent's HLSL2GLSL
        /// converter (used to cross-compile to the OpenGL device type) recognizes and strips only
        /// the pragma form, passing an inline `row_major` qualifier through into the GLSL output
        /// completely unchanged -- which is not valid GLSL syntax and fails to compile there.
        constexpr const char* kConstantsHlsl = R"(
#pragma pack_matrix(row_major)
cbuffer Constants
{
    float4x4 g_WorldViewProj;
    float4x4 g_World;
    float4 g_DiffuseColor;
    // Kept apart deliberately (DILIGENT-59): g_Ambient joins the per-light diffuse sum, which then
    // gets multiplied by g_DiffuseColor once; g_Emissive is added to that result afterward and must
    // never be multiplied by g_DiffuseColor (or a texture sample times it) a second time.
    float4 g_Ambient;
    float4 g_Emissive;
    float4 g_EyePositionSpecularPower;
    float4 g_SpecularColor;
    float4 g_LightDir[3];
    float4 g_LightDiffuse[3];
    float4 g_LightSpecular[3];
    float4 g_Flags;
    float4 g_AlphaTest;
    float4 g_FogVector;
    float4 g_FogColor;
    /// x = envMapAmount, y = fresnelEnabled, z = fresnelFactor, w unused.
    float4 g_EnvMapParams;
    float4 g_EnvMapSpecular;
};

/// FNA's fog term: dot the object-space position with the fog vector, then keep = 1 - saturate(it).
/// An all-zero fog vector (fog disabled) yields keep = 1, a true no-op.
float ComputeFogKeep(float3 objectPosition)
{
    return 1.0 - saturate(dot(float4(objectPosition, 1.0), g_FogVector));
}
)";

        /// Appended to pixel shaders only. `discard` is not valid in a vertex shader, so this
        /// cannot live in the shared constants block above even though every stage includes that.
constexpr const char* kPixelHelpersHlsl = R"(
float3 CnaSrgbToLinear(float3 color)
{
    float3 low = color / 12.92;
    // Use a vector exponent: Diligent's HLSL2GLSL path preserves pow() literally, while GLSL
    // (unlike HLSL) requires both pow operands to have the same vector width.
    float3 high = pow((color + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return lerp(low, high, step(0.04045, color));
}

float3 CnaLinearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float exponent = 1.0 / 2.4;
    float3 high = 1.055 * pow(max(color, 0.0), float3(exponent, exponent, exponent)) - 0.055;
    return lerp(low, high, step(0.0031308, color));
}

/// XNA alpha test, in the encoding GpuDrawParams::alphaTest documents:
///   tolerance > 0 -> pass = |alpha - reference| < tolerance
///   otherwise     -> pass = alpha < reference
/// then the pass/fail weight decides whether the pixel survives. {0,0,1,1} never discards.
/// Fog is applied afterwards, RGB only, exactly as the other CNA renderers do.
float4 FinishPixel(float4 color, float fogKeep)
{
    bool passesAlphaTest = (g_AlphaTest.y > 0.0) ? (abs(color.a - g_AlphaTest.x) < g_AlphaTest.y)
                                                 : (color.a < g_AlphaTest.x);
    float weight = passesAlphaTest ? g_AlphaTest.z : g_AlphaTest.w;
    if (weight < 0.0)
        discard;
    // g_FogColor.w is zero for every ordinary effect. PBR repurposes that otherwise-unused lane
    // as its output-transfer flag, so fog and the final OETF stay in the same linear workflow.
    float3 fogColor = lerp(g_FogColor.rgb, CnaSrgbToLinear(g_FogColor.rgb), g_FogColor.w);
    color.rgb = lerp(fogColor, color.rgb, fogKeep);
    color.rgb = lerp(color.rgb, CnaLinearToSrgb(color.rgb), g_FogColor.w);
    return color;
}

// Mirrors kVertexLightingHlsl's own helper of the same name (defined again here, not shared,
// since vertex and pixel shaders are separate HLSL compilations) -- see that copy's own comment
// for why a disabled light's exactly-zero direction must not reach a plain normalize().
float3 SafeNormalizeLightDir(float3 v)
{
    float lenSq = dot(v, v);
    return (lenSq > 1e-12) ? (v / sqrt(lenSq)) : v;
}
)";

        constexpr const char* kSpriteVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float2 UV    : ATTRIB1;
    float4 Color : ATTRIB2;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos   = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV    = vsIn.UV;
    psIn.Color = vsIn.Color;
}
)";

        constexpr const char* kSpritePixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    psOut.Color = g_Texture.Sample(g_Texture_sampler, psIn.UV) * psIn.Color;
}
)";

        constexpr const char* kColoredVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos     = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.Color   = vsIn.Color * g_DiffuseColor;
    psIn.FogKeep = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kColoredPixelHlsl = R"(
struct PSInput
{
    float4 Pos     : SV_POSITION;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    psOut.Color = FinishPixel(psIn.Color, psIn.FogKeep);
}
)";

        constexpr const char* kTexturedVertexHlsl = R"(
struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV  : ATTRIB1;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos     = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV      = vsIn.UV;
    psIn.Color   = g_DiffuseColor;
    psIn.FogKeep = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kColoredTexturedVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
    float2 UV    : ATTRIB2;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos     = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV      = vsIn.UV;
    psIn.Color   = vsIn.Color * g_DiffuseColor;
    psIn.FogKeep = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kTexturedPixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 texel = g_Texture.Sample(g_Texture_sampler, psIn.UV);
    psOut.Color = FinishPixel(lerp(psIn.Color, texel * psIn.Color, g_Flags.x), psIn.FogKeep);
}
)";

        /// Blinn-Phong lighting over the three XNA directional lights, evaluated per pixel. The
        /// light direction/diffuse/specular vectors are already zeroed by the effect layer for a
        /// disabled light, so no per-light enable flag is needed here.
        constexpr const char* kLitVertexHlsl = R"(
struct VSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV     : ATTRIB2;
};

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEX_COORD;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
    float  FogKeep  : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos      = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.WorldPos = mul(float4(vsIn.Pos, 1.0), g_World).xyz;
    // DILIGENT-59: World's inverse-transpose, not World itself -- correct under non-uniform scale
    // (a plain World transform of the normal is only correct for uniform scale/pure rotation).
    // Matches kSkinnedVertexHlsl's own established pattern.
    float3x3 worldNormalMat = InverseTranspose3x3(float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz));
    psIn.Normal   = normalize(mul(vsIn.Normal, worldNormalMat));
    psIn.UV       = vsIn.UV;
    psIn.FogKeep  = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kLitPixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEX_COORD;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
    float  FogKeep  : FOG_KEEP;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 baseColor = g_DiffuseColor;
    if (g_Flags.x > 0.5)
        baseColor *= g_Texture.Sample(g_Texture_sampler, psIn.UV);

    if (g_Flags.z < 0.5)
    {
        psOut.Color = FinishPixel(baseColor, psIn.FogKeep);
        return;
    }

    float3 normal   = normalize(psIn.Normal);
    float3 eyeDir   = normalize(g_EyePositionSpecularPower.xyz - psIn.WorldPos);
    float3 diffuse  = g_Ambient.rgb;
    float3 specular = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 3; ++i)
    {
        float3 lightDir = SafeNormalizeLightDir(-g_LightDir[i].xyz);
        float  nDotL    = max(dot(normal, lightDir), 0.0);
        diffuse += g_LightDiffuse[i].rgb * nDotL;
        if (nDotL > 0.0)
        {
            float3 halfVec = normalize(lightDir + eyeDir);
            float  nDotH   = max(dot(normal, halfVec), 0.0);
            specular += g_LightSpecular[i].rgb * pow(nDotH, max(g_EyePositionSpecularPower.w, 1.0));
        }
    }

    // DILIGENT-59: g_Emissive is added AFTER the ambient/light-sum*DiffuseColor multiply, never
    // multiplied by it (or by the bound texture, already folded into baseColor) a second time.
    // Specular is scaled by the FINAL output alpha (baseColor.a, matching FNA's own AddSpecular
    // macro), not left unscaled.
    float3 lit = baseColor.rgb * diffuse + g_Emissive.rgb;
    lit += specular * g_SpecularColor.rgb * baseColor.a;
    psOut.Color = FinishPixel(float4(lit, baseColor.a), psIn.FogKeep);
}
)";

        /// DILIGENT-37: real XNA's own default (`BasicEffect`/`SkinnedEffect`'s
        /// `PreferPerPixelLighting == false`) evaluates Blinn-Phong once per vertex and
        /// Gouraud-interpolates the result, rather than re-evaluating it per fragment the way
        /// kLitPixelHlsl above does. Extracted from kLitPixelHlsl's own inline math unchanged --
        /// only the STAGE it runs in differs between the two -- so kLitVertexLitVertexHlsl and
        /// kSkinnedVertexLitVertexHlsl below both call this from the vertex stage instead. Always
        /// prepended alongside kConstantsHlsl (see the assembly point below): harmless dead code in
        /// variants that never call it, since every uniform it reads is already declared there.
        ///
        /// Also hosts InverseTranspose3x3() (DILIGENT-59): every lit vertex shader needs it for a
        /// correct world-space normal under non-uniform scale, not just the skinned ones kBonesHlsl
        /// originally scoped it to, so it lives here instead -- always prepended, unlike kBonesHlsl
        /// which is conditional on usesBones. Defined exactly once: kBonesHlsl's own former copy
        /// would now collide with this one for every skinned variant, which prepends both.
        constexpr const char* kVertexLightingHlsl = R"(
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float determinant = dot(m[0], c0);
    float invDeterminant = (abs(determinant) > 1e-8) ? (1.0 / determinant) : 0.0;
    return float3x3(c0 * invDeterminant, c1 * invDeterminant, c2 * invDeterminant);
}

float3 CnaSkinNormal(float3x3 m, float3 n)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float determinant = dot(m[0], c0);
    float3 transformed = mul(n, float3x3(c0, c1, c2));
    return abs(determinant) > 1e-6
        ? transformed * sign(determinant)
        : mul(n, m);
}

float CnaDirectionHandedness(float3x3 m)
{
    return dot(m[0], cross(m[1], m[2])) < 0.0 ? -1.0 : 1.0;
}

// A disabled light's direction is left at exactly (0,0,0) by the effect layer (see this file's own
// note on that convention); plain normalize() of a zero-length vector is undefined/NaN, which would
// otherwise poison the whole per-light sum through dot()/max() once every enabled light's own math
// runs alongside it. Non-zero vectors get treated the same as normalize() would.
float3 SafeNormalizeLightDir(float3 v)
{
    float lenSq = dot(v, v);
    return (lenSq > 1e-12) ? (v / sqrt(lenSq)) : v;
}

// DILIGENT-59: litDiffuse is g_Ambient + the per-light sum ONLY -- g_Emissive is deliberately never
// folded in here (it is a per-draw constant, not per-vertex, and must be added by the pixel stage
// AFTER the light-sum*DiffuseColor multiply, never multiplied by it). Light directions are
// normalized defensively: CNA's own DirectionalLight.Direction setter (unlike real XNA's) does not
// normalize on assignment.
void ComputeVertexLighting(float3 worldPos, float3 normal, out float3 litDiffuse, out float3 litSpecular)
{
    float3 eyeDir = normalize(g_EyePositionSpecularPower.xyz - worldPos);
    litDiffuse  = g_Ambient.rgb;
    litSpecular = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 3; ++i)
    {
        float3 lightDir = SafeNormalizeLightDir(-g_LightDir[i].xyz);
        float  nDotL    = max(dot(normal, lightDir), 0.0);
        litDiffuse += g_LightDiffuse[i].rgb * nDotL;
        if (nDotL > 0.0)
        {
            float3 halfVec = normalize(lightDir + eyeDir);
            float  nDotH   = max(dot(normal, halfVec), 0.0);
            litSpecular += g_LightSpecular[i].rgb * pow(nDotH, max(g_EyePositionSpecularPower.w, 1.0));
        }
    }
}
)";

        constexpr const char* kLitVertexLitVertexHlsl = R"(
struct VSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV     : ATTRIB2;
};

struct PSInput
{
    float4 Pos         : SV_POSITION;
    float2 UV          : TEX_COORD;
    float3 LitDiffuse  : LIT_DIFFUSE;
    float3 LitSpecular : LIT_SPECULAR;
    float  FogKeep     : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos      = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    float3 worldPos = mul(float4(vsIn.Pos, 1.0), g_World).xyz;
    // DILIGENT-59: World's inverse-transpose, not World itself -- see kLitVertexHlsl's own note.
    float3x3 worldNormalMat = InverseTranspose3x3(float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz));
    float3 normal   = normalize(mul(vsIn.Normal, worldNormalMat));
    ComputeVertexLighting(worldPos, normal, psIn.LitDiffuse, psIn.LitSpecular);
    psIn.UV       = vsIn.UV;
    psIn.FogKeep  = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kLitVertexLitPixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos         : SV_POSITION;
    float2 UV          : TEX_COORD;
    float3 LitDiffuse  : LIT_DIFFUSE;
    float3 LitSpecular : LIT_SPECULAR;
    float  FogKeep     : FOG_KEEP;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 baseColor = g_DiffuseColor;
    if (g_Flags.x > 0.5)
        baseColor *= g_Texture.Sample(g_Texture_sampler, psIn.UV);

    // DILIGENT-59: g_Emissive added after psIn.LitDiffuse (ambient+lights, computed per-vertex by
    // ComputeVertexLighting -- never includes emissive) is multiplied by baseColor; specular scaled
    // by the final output alpha, matching kLitPixelHlsl's own per-pixel-lit formula exactly (this
    // is its per-vertex-lit sibling, same math, only the evaluation frequency differs).
    float3 lit = baseColor.rgb * psIn.LitDiffuse + g_Emissive.rgb;
    lit += psIn.LitSpecular * g_SpecularColor.rgb * baseColor.a;
    psOut.Color = FinishPixel(float4(lit, baseColor.a), psIn.FogKeep);
}
)";

        /// Identical to kSkinnedVertexHlsl except the last step: instead of handing the pixel stage
        /// a raw WorldPos/Normal to re-light every fragment, it calls the same
        /// ComputeVertexLighting() kLitVertexLitVertexHlsl uses and hands the pixel stage the
        /// already-lit diffuse/specular accumulators (kLitVertexLitPixelHlsl, shared with that
        /// variant -- both PSInput layouts are identical).
        constexpr const char* kSkinnedVertexLitVertexHlsl = R"(
struct VSInput
{
    float3 Pos          : ATTRIB0;
    float3 Normal       : ATTRIB1;
    float2 UV           : ATTRIB2;
    float4 BlendWeights : ATTRIB3;
    uint4  BlendIndices : ATTRIB4;
};

struct PSInput
{
    float4 Pos         : SV_POSITION;
    float2 UV          : TEX_COORD;
    float3 LitDiffuse  : LIT_DIFFUSE;
    float3 LitSpecular : LIT_SPECULAR;
    float  FogKeep     : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    float4x4 skin = ComputeSkinMatrix(vsIn.BlendWeights, vsIn.BlendIndices, g_Flags.w);
    float4 skinnedPos = mul(float4(vsIn.Pos, 1.0), skin);

    psIn.Pos = mul(skinnedPos, g_WorldViewProj);
    float3 worldPos = mul(skinnedPos, g_World).xyz;

    float3 skinnedNormal = mul(vsIn.Normal, float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz));
    float3 normal = normalize(mul(skinnedNormal, InverseTranspose3x3(float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz))));
    ComputeVertexLighting(worldPos, normal, psIn.LitDiffuse, psIn.LitSpecular);
    psIn.UV      = vsIn.UV;
    psIn.FogKeep = ComputeFogKeep(skinnedPos.xyz);
}
)";

        /// DILIGENT-36 (PbrEffect, CNAEXT): stride 48 (position + normal + tangent [xyz + glTF
        /// bitangent-handedness sign in w] + UV), HLSL port of this project's own established
        /// PBR reference (src/CNA/Internal/Renderers/D3DCommon/shaders/pbr3d.vert.hlsl), unchanged
        /// except for Diligent's shared-cbuffer naming (g_WorldViewProj/g_World in place of a
        /// per-variant Mvp/World pair).
        constexpr const char* kPbrVertexHlsl = R"(
struct VSInput
{
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1;
    float4 Tangent : ATTRIB2;
    float2 UV      : ATTRIB3;
    /* CNA_PBR_UV1_RIGID_ATTRIBUTE */
};

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV       : TEX_COORD;
    /* CNA_PBR_UV1_INTERPOLANT */
    float  FogKeep  : FOG_KEEP;
    float3 WorldPos : WORLD_POS;
};

float3x3 PbrInverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float determinant = dot(m[0], c0);
    float invDeterminant = (abs(determinant) > 1e-8) ? (1.0 / determinant) : 0.0;
    return float3x3(c0 * invDeterminant, c1 * invDeterminant, c2 * invDeterminant);
}

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);

    float3x3 normalMatrix = PbrInverseTranspose3x3(float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz));
    psIn.Normal = normalize(mul(vsIn.Normal, normalMatrix));

    // Tangent transforms as a plain direction under World (not the inverse-transpose used for the
    // normal above) -- correct for uniform-scale World transforms, matching this renderer's own
    // established pbr3d.vert.hlsl reference exactly.
    float3x3 worldDirectionMat = float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz);
    psIn.Tangent = float4(mul(vsIn.Tangent.xyz, worldDirectionMat),
                          vsIn.Tangent.w * CnaDirectionHandedness(worldDirectionMat));

    psIn.UV       = vsIn.UV;
    /* CNA_PBR_UV1_ASSIGNMENT */
    psIn.WorldPos = mul(float4(vsIn.Pos, 1.0), g_World).xyz;
    psIn.FogKeep  = ComputeFogKeep(vsIn.Pos);
}
)";

        /// PbrEffect's fragment stage: GGX/Trowbridge-Reitz normal distribution,
        /// Smith-Schlick-GGX visibility, Schlick Fresnel -- glTF 2.0's own reference BRDF, term for
        /// term identical to pbr3d.frag.hlsl. g_PbrAmbientMetallic/g_PbrEmissiveRoughness come from
        /// their own PbrConstants buffer (see UploadPbrConstants()) rather than the shared Constants
        /// block's g_Ambient/g_Emissive: PBR's own formula (ambient scales albedo*occlusion,
        /// emissive is added standalone, unscaled) doesn't fit that pair's own contract either.
        constexpr const char* kPbrPixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;
Texture2D    g_NormalMap;
SamplerState g_NormalMap_sampler;
Texture2D    g_MetallicRoughnessMap;
SamplerState g_MetallicRoughnessMap_sampler;
Texture2D    g_EmissiveMap;
SamplerState g_EmissiveMap_sampler;
Texture2D    g_OcclusionMap;
SamplerState g_OcclusionMap_sampler;
Texture2D    g_SpecularMap;
SamplerState g_SpecularMap_sampler;
Texture2D    g_SpecularColorMap;
SamplerState g_SpecularColorMap_sampler;

cbuffer PbrConstants
{
    float4 g_PbrAmbientMetallic;   // xyz = ambient colour, w = metallic factor
    float4 g_PbrEmissiveRoughness; // xyz = emissive colour, w = roughness factor
    // x = normal scale, y = occlusion strength, z = decode base, w = decode emissive.
    float4 g_PbrMapScales;
    // xyz = unclamped dielectric F0 before the colour texture, w = specular factor.
    float4 g_PbrDielectricFresnel;
    // x = decode specular-colour texture, y = seven-bit TEXCOORD_1 selector mask.
    float4 g_PbrSpecularState;
    float4 g_PbrTextureTransformRows[10]; // two affine UV rows per PBR map
    float4 g_PbrSpecularTextureTransformRows[4];
};

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV       : TEX_COORD;
    /* CNA_PBR_UV1_INTERPOLANT */
    float  FogKeep  : FOG_KEEP;
    float3 WorldPos : WORLD_POS;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

static const float kPbrPi = 3.14159265;

float2 CnaPbrTransformUv(float2 uv, int slot)
{
    float3 value = float3(uv, 1.0);
    return float2(dot(value, g_PbrTextureTransformRows[slot * 2].xyz),
                  dot(value, g_PbrTextureTransformRows[slot * 2 + 1].xyz));
}

float2 CnaPbrSpecularTransformUv(float2 uv, int slot)
{
    float3 value = float3(uv, 1.0);
    return float2(dot(value, g_PbrSpecularTextureTransformRows[slot * 2].xyz),
                  dot(value, g_PbrSpecularTextureTransformRows[slot * 2 + 1].xyz));
}

float2 CnaPbrUv(PSInput value, int slot)
{
    /* CNA_PBR_UV_SELECTOR */
}

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
    float D = a2 / (kPbrPi * dTerm * dTerm + 1e-7);
    float k = (roughness + 1.0); k = k * k / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    float3 F = F0 + (F90 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 diffuseColor = albedo * (1.0 - metallic);
    float3 kd = float3(1.0, 1.0, 1.0) - F;
    return (kd * diffuseColor / kPbrPi + specular) * lightColor * NdotL;
}

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 baseColorTex = g_Texture.Sample(
        g_Texture_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 0), 0));
    float3 baseColor = lerp(baseColorTex.rgb, CnaSrgbToLinear(baseColorTex.rgb),
                            g_PbrMapScales.z);
    float3 albedo = baseColor * g_DiffuseColor.rgb;
    float alpha = baseColorTex.a * g_DiffuseColor.a;

    float3 N = normalize(psIn.Normal);
    float3 T = normalize(psIn.Tangent.xyz - N * dot(N, psIn.Tangent.xyz));
    float3 B = cross(N, T) * psIn.Tangent.w;
    float3 sampledNormal = g_NormalMap.Sample(
        g_NormalMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 1), 1)).rgb * 2.0 - 1.0;
    sampledNormal.xy *= g_PbrMapScales.x;
    // Spell out the tangent-basis transform. HLSL-to-GLSL conversion otherwise disagrees with
    // native HLSL/SPIR-V about mul(float3, float3x3)'s row/column interpretation for non-axis-
    // aligned normals, while this linear combination states the intended basis unambiguously.
    float3 finalNormal = normalize(sampledNormal.x * T + sampledNormal.y * B + sampledNormal.z * N);

    float4 mr = g_MetallicRoughnessMap.Sample(
        g_MetallicRoughnessMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 2), 2));
    float roughness = clamp(mr.g * g_PbrEmissiveRoughness.w, 0.045, 1.0);
    float metallic  = clamp(mr.b * g_PbrAmbientMetallic.w, 0.0, 1.0);

    float3 V = normalize(g_EyePositionSpecularPower.xyz - psIn.WorldPos);
    float specularWeight = g_PbrDielectricFresnel.w * g_SpecularMap.Sample(
        g_SpecularMap_sampler,
        CnaPbrSpecularTransformUv(CnaPbrUv(psIn, 5), 0)).a;
    float3 specularColorTex = g_SpecularColorMap.Sample(
        g_SpecularColorMap_sampler,
        CnaPbrSpecularTransformUv(CnaPbrUv(psIn, 6), 1)).rgb;
    specularColorTex = lerp(specularColorTex, CnaSrgbToLinear(specularColorTex),
                            g_PbrSpecularState.x);
    float3 dielectricF0 = min(g_PbrDielectricFresnel.xyz * specularColorTex,
                              float3(1.0, 1.0, 1.0)) * specularWeight;
    float3 F0 = lerp(dielectricF0, albedo, metallic);
    float3 F90 = lerp(float3(specularWeight, specularWeight, specularWeight),
                      float3(1.0, 1.0, 1.0), metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    Lo += PbrLight(finalNormal, V, normalize(-g_LightDir[0].xyz), g_LightDiffuse[0].xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-g_LightDir[1].xyz), g_LightDiffuse[1].xyz, albedo, F0, F90, roughness, metallic);
    Lo += PbrLight(finalNormal, V, normalize(-g_LightDir[2].xyz), g_LightDiffuse[2].xyz, albedo, F0, F90, roughness, metallic);

    float occlusion = g_OcclusionMap.Sample(
        g_OcclusionMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 4), 4)).r;
    occlusion = 1.0 + g_PbrMapScales.y * (occlusion - 1.0);
    float3 ambient = g_PbrAmbientMetallic.xyz * albedo * occlusion;
    float3 emissiveSample = g_EmissiveMap.Sample(
        g_EmissiveMap_sampler, CnaPbrTransformUv(CnaPbrUv(psIn, 3), 3)).rgb;
    emissiveSample = lerp(emissiveSample, CnaSrgbToLinear(emissiveSample), g_PbrMapScales.w);
    float3 emissive = g_PbrEmissiveRoughness.xyz * emissiveSample;

    psOut.Color = FinishPixel(float4(ambient + Lo + emissive, alpha), psIn.FogKeep);
}
)";

        /// SkinnedPbrEffect (stride 68: kPbrVertexHlsl's own layout with WeightsPerVertex-summed
        /// bone skinning applied to Position/Normal/Tangent before the World transform, mirroring
        /// kSkinnedVertexHlsl's own skinning math exactly). The pixel stage is pure PBR math with
        /// no skinning awareness of its own -- kPbrPixelHlsl is reused unchanged.
        constexpr const char* kSkinnedPbrVertexHlsl = R"(
struct VSInput
{
    float3 Pos          : ATTRIB0;
    float3 Normal       : ATTRIB1;
    float4 Tangent      : ATTRIB2;
    float2 UV           : ATTRIB3;
    float4 BlendWeights : ATTRIB4;
    uint4  BlendIndices : ATTRIB5;
    /* CNA_PBR_UV1_SKINNED_ATTRIBUTE */
};

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV       : TEX_COORD;
    /* CNA_PBR_UV1_INTERPOLANT */
    float  FogKeep  : FOG_KEEP;
    float3 WorldPos : WORLD_POS;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    float4x4 skin = ComputeSkinMatrix(vsIn.BlendWeights, vsIn.BlendIndices, g_Flags.w);
    float4 skinnedPos = mul(float4(vsIn.Pos, 1.0), skin);

    psIn.Pos = mul(skinnedPos, g_WorldViewProj);

    // GLTF-264: inverse-transpose the complete blended joint matrix before composing it with
    // World's inverse-transpose. Tangents remain ordinary directions on both matrices.
    float3x3 skinNormalMat = float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz);
    float3x3 worldDirectionMat = float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz);
    float3x3 worldNormalMat = InverseTranspose3x3(worldDirectionMat);
    psIn.Normal = normalize(mul(CnaSkinNormal(skinNormalMat, vsIn.Normal), worldNormalMat));
    psIn.Tangent = float4(mul(mul(vsIn.Tangent.xyz, skinNormalMat), worldDirectionMat),
                          vsIn.Tangent.w * CnaDirectionHandedness(worldDirectionMat)
                              * CnaDirectionHandedness(skinNormalMat));

    psIn.UV       = vsIn.UV;
    /* CNA_PBR_UV1_ASSIGNMENT */
    psIn.WorldPos = mul(skinnedPos, g_World).xyz;
    psIn.FogKeep  = ComputeFogKeep(skinnedPos.xyz);
}
)";

        constexpr const char* kDualTextureVertexHlsl = R"(
struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV  : ATTRIB1;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos     = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV      = vsIn.UV;
    psIn.Color   = g_DiffuseColor;
    psIn.FogKeep = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kDualTextureColoredVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
    float2 UV    : ATTRIB2;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos     = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV      = vsIn.UV;
    psIn.Color   = vsIn.Color * g_DiffuseColor;
    psIn.FogKeep = ComputeFogKeep(vsIn.Pos);
}
)";

        constexpr const char* kDualTexturePixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;
Texture2D    g_Texture2;
SamplerState g_Texture2_sampler;

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float4 Color   : COLOR0;
    float  FogKeep : FOG_KEEP;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 layer0 = g_Texture.Sample(g_Texture_sampler, psIn.UV);
    float4 layer1 = g_Texture2.Sample(g_Texture2_sampler, psIn.UV);
    // XNA's DualTextureEffect doubles the first layer before the modulate, so a mid-grey second
    // layer leaves the first unchanged.
    layer0.rgb *= 2.0;
    psOut.Color = FinishPixel(layer0 * layer1 * psIn.Color, psIn.FogKeep);
}
)";

        /// Reuses the lit vertex layout (position, normal, UV): an environment map needs the same
        /// world-space normal and eye vector the lit shader already computes.
        constexpr const char* kEnvironmentMapPixelHlsl = R"(
Texture2D      g_Texture;
SamplerState   g_Texture_sampler;
TextureCube    g_EnvMap;
SamplerState   g_EnvMap_sampler;

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEX_COORD;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
    float  FogKeep  : FOG_KEEP;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float3 normal = normalize(psIn.Normal);
    float3 eyeDir = normalize(g_EyePositionSpecularPower.xyz - psIn.WorldPos);

    // DILIGENT-59: g_Ambient joins the light sum (multiplied by DiffuseColor below, same as every
    // other lit variant); g_Emissive is EnvironmentMapEffect::FillGpuDrawParams()'s own pre-baked
    // EmissiveColor+AmbientLightColor*DiffuseColor value and must be added AFTER that multiply, not
    // multiplied by DiffuseColor (or the texture sample) again.
    float3 lightSum = g_Ambient.rgb;
    for (int i = 0; i < 3; ++i)
        lightSum += g_LightDiffuse[i].rgb * max(dot(normal, SafeNormalizeLightDir(-g_LightDir[i].xyz)), 0.0);

    float4 texel = g_Texture.Sample(g_Texture_sampler, psIn.UV);
    float3 litDiffuse = lightSum * g_DiffuseColor.rgb + g_Emissive.rgb;
    float3 baseColor = litDiffuse * texel.rgb;
    float combinedAlpha = g_DiffuseColor.a * texel.a;

    float4 envSample = g_EnvMap.Sample(g_EnvMap_sampler, reflect(-eyeDir, normal));
    // Flat envMapAmount, or edge-weighted by view angle when FresnelEnabled -- FNA's own
    // PSEnvMap/PSEnvMapSpecular split.
    float blendFactor = (g_EnvMapParams.y > 0.5)
        ? pow(max(1.0 - abs(dot(eyeDir, normal)), 0.0), g_EnvMapParams.z) * g_EnvMapParams.x
        : g_EnvMapParams.x;

    float3 rgb = lerp(baseColor, envSample.rgb * combinedAlpha, blendFactor)
               + g_EnvMapSpecular.rgb * envSample.a * combinedAlpha;
    psOut.Color = FinishPixel(float4(rgb, combinedAlpha), psIn.FogKeep);
}
)";

        /// The bone palette lives in its own constant buffer: 72 matrices is 4.5 KB, far too much
        /// to append to the per-draw block every non-skinned draw also uploads. Row-major via the
        /// `#pragma pack_matrix(row_major)` in kConstantsHlsl, always prepended before this block --
        /// see that constant's own doc comment for why the pragma form is used over an inline
        /// `row_major` qualifier here.
        constexpr const char* kBonesHlsl = R"(
cbuffer Bones
{
    float4x4 g_Bones[72];
};

/// FNA's Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2 or 4) weight/index pairs.
float4x4 ComputeSkinMatrix(float4 weights, uint4 indices, float weightsPerVertex)
{
    float4x4 skin = g_Bones[indices.x] * weights.x;
    if (weightsPerVertex >= 2.0)
        skin += g_Bones[indices.y] * weights.y;
    if (weightsPerVertex >= 4.0)
        skin += g_Bones[indices.z] * weights.z + g_Bones[indices.w] * weights.w;
    return skin;
}
)";

        constexpr const char* kSkinnedVertexHlsl = R"(
struct VSInput
{
    float3 Pos          : ATTRIB0;
    float3 Normal       : ATTRIB1;
    float2 UV           : ATTRIB2;
    float4 BlendWeights : ATTRIB3;
    uint4  BlendIndices : ATTRIB4;
};

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEX_COORD;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
    float  FogKeep  : FOG_KEEP;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    float4x4 skin = ComputeSkinMatrix(vsIn.BlendWeights, vsIn.BlendIndices, g_Flags.w);
    float4 skinnedPos = mul(float4(vsIn.Pos, 1.0), skin);

    psIn.Pos      = mul(skinnedPos, g_WorldViewProj);
    psIn.WorldPos = mul(skinnedPos, g_World).xyz;

    float3 skinnedNormal = mul(vsIn.Normal, float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz));
    psIn.Normal   = normalize(mul(skinnedNormal, InverseTranspose3x3(float3x3(g_World[0].xyz, g_World[1].xyz, g_World[2].xyz))));
    psIn.UV       = vsIn.UV;
    psIn.FogKeep  = ComputeFogKeep(skinnedPos.xyz);
}
)";

        /// Hardware instancing (DILIGENT-43): only Position is read from the per-vertex stream
        /// (slot 0); the per-instance stream (slot 1) supplies one 4x4 world matrix as four
        /// consecutive float4 rows. Deliberately minimal -- flat g_DiffuseColor output, no texture,
        /// no lighting, no fog -- matching every other CNA renderer's own hardware-instancing
        /// baseline (see e.g. src/CNA/Internal/Renderers/D3DCommon/shaders/instanced3d.vert.hlsl).
        /// g_WorldViewProj is repurposed to hold View*Projection here (there is no single shared
        /// World matrix to fold in, unlike every other variant's constant of the same name) --
        /// DrawInstancedPrimitivesEx() uploads it that way.
        constexpr const char* kInstancedVertexHlsl = R"(
struct VSInput
{
    float3 Pos      : ATTRIB0;
    float4 InstRow0 : ATTRIB1;
    float4 InstRow1 : ATTRIB2;
    float4 InstRow2 : ATTRIB3;
    float4 InstRow3 : ATTRIB4;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    float4x4 world = float4x4(vsIn.InstRow0, vsIn.InstRow1, vsIn.InstRow2, vsIn.InstRow3);
    float4 worldPos = mul(float4(vsIn.Pos, 1.0), world);
    psIn.Pos   = mul(worldPos, g_WorldViewProj);
    psIn.Color = g_DiffuseColor;
}
)";

        constexpr const char* kInstancedPixelHlsl = R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

float4 main(in PSInput psIn) : SV_Target
{
    return psIn.Color;
}
)";
    }
}
