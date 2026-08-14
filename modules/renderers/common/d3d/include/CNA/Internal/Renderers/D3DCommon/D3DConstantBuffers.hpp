#pragma once

// plan_dx.md Phase DIRECTX8 (DX-60/DX-60a): explicit GPU-side constant-buffer POD structs matching the
// real HLSL `cbuffer` declarations DX-13-hlsl already wrote (register scheme recorded in that
// task's own plan row) -- NOT a raw memcpy of the whole-surface GpuDrawParams convenience struct.
// Each struct's field order/offsets are verified via static_assert against the actual HLSL source
// (not re-derived/guessed), and each buffer's total size is a 16-byte multiple, as D3D11 requires
// for a constant buffer's ByteWidth.
//
// Header-only, no .cpp: these are pure data-layout structs shared (design decision 4) with a
// future D3D12 consumer, which reads the same DXBC-compiled cbuffer byte layout via a different
// (root-signature/CBV) binding model -- nothing here depends on the D3D11 API itself.

#include <cstddef>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// Matches colored3d.vert.hlsl / textured3d.vert.hlsl / colored_textured3d.vert.hlsl's shared
    /// `cbuffer PerDraw : register(b0)` byte-for-byte (128 bytes / 32 floats -- mirrors the
    /// pre-existing Vulkan push-constant layout these three variants' GLSL sources already used,
    /// per DX-13-hlsl's own row notes). colored3d itself only reads Mvp/DiffuseColor/
    /// VertexColorEnabled (its HLSL leaves the rest as opaque `_Unused...` blobs) -- the named
    /// fields here are the textured3d/colored_textured3d superset, which colored3d callers simply
    /// leave zero-initialized.
    struct alignas(16) D3DPerDrawConstants
    {
        float Mvp[16];              ///< row_major float4x4, offset 0 (64 bytes). Upload via
                                     ///< Matrix::ToColumnMajor() -- despite the name, that method
                                     ///< emits the raw row-major M11..M44 byte order XNA's CPU-side
                                     ///< Matrix already uses, which is exactly what an HLSL
                                     ///< `row_major` cbuffer field expects unchanged (design
                                     ///< decision 14 -- no CPU-side transpose).
        float DiffuseColor[4];      ///< offset 64
        float AmbientColor[3];      ///< offset 80
        float LightingEnabled;      ///< offset 92
        float Light0Dir[3];         ///< offset 96
        float TextureEnabled;       ///< offset 108
        float Light0Diffuse[3];     ///< offset 112
        float VertexColorEnabled;   ///< offset 124
    };
    static_assert(sizeof(D3DPerDrawConstants) == 128, "D3DPerDrawConstants must match PerDraw's real 128-byte HLSL cbuffer size");
    static_assert(offsetof(D3DPerDrawConstants, DiffuseColor) == 64, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPerDrawConstants, AmbientColor) == 80, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPerDrawConstants, LightingEnabled) == 92, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPerDrawConstants, Light0Dir) == 96, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPerDrawConstants, TextureEnabled) == 108, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPerDrawConstants, Light0Diffuse) == 112, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPerDrawConstants, VertexColorEnabled) == 124, "D3DPerDrawConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DPerDrawConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// Matches colored3d.vert.hlsl / textured3d.vert.hlsl / colored_textured3d.vert.hlsl's shared
    /// `cbuffer FogParams : register(b1)` byte-for-byte (32 bytes / 8 floats). dual_texture3d also
    /// declares an identically-shaped FogParams, just at register(b2) instead -- same struct, this
    /// header only fixes the byte layout, not the register slot a given draw call binds it to.
    struct alignas(16) D3DFogConstants
    {
        float FogColor[4];   ///< offset 0: xyz = FogColor, w = reserved padding
        float FogVector[4];  ///< offset 16: CPU-prepared FNA view-space fog vector
    };
    static_assert(sizeof(D3DFogConstants) == 32, "D3DFogConstants must match FogParams's real 32-byte HLSL cbuffer size");
    static_assert(offsetof(D3DFogConstants, FogColor) == 0, "D3DFogConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DFogConstants, FogVector) == 16, "D3DFogConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DFogConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// Matches lit_textured3d.vert.hlsl's `cbuffer LitLightParams : register(b1)` byte-for-byte
    /// (256 bytes / 64 floats) -- DirectionalLight1/2, EmissiveColor, the World matrix (kept out of
    /// PerDraw since lighting needs true world-space position/normal, not just the MVP-collapsed
    /// clip position), eye position, per-light specular, and fog, all forwarded via this second
    /// constant buffer since PerDraw above is already fully packed for the lit variant too.
    /// NOT YET WIRED into any draw call -- DX-60 defines this layout ahead of DX-63 (lit_textured3d
    /// pipeline wiring) landing it, same "get the layout right once" discipline DX-60's own plan
    /// row calls for.
    struct alignas(16) D3DLightingConstants
    {
        float Light1Dir[4];            ///< offset 0:   xyz + pad
        float Light1Diffuse[4];        ///< offset 16
        float Light2Dir[4];            ///< offset 32
        float Light2Diffuse[4];        ///< offset 48
        float EmissiveColor[4];        ///< offset 64:  xyz + pad
        float World[16];               ///< offset 80:  row_major float4x4 (64 bytes)
        float EyePosition[4];          ///< offset 144: xyz + pad
        float Light0Specular[4];       ///< offset 160
        float Light1Specular[4];       ///< offset 176
        float Light2Specular[4];       ///< offset 192
        float SpecularColorPower[4];   ///< offset 208: xyz = SpecularColor, w = SpecularPower
        float FogColor[4];            ///< offset 224: xyz = FogColor, w = reserved padding
        float FogVector[4];           ///< offset 240: CPU-prepared FNA view-space fog vector
    };
    static_assert(sizeof(D3DLightingConstants) == 256, "D3DLightingConstants must match LitLightParams's real 256-byte HLSL cbuffer size");
    static_assert(offsetof(D3DLightingConstants, World) == 80, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, EyePosition) == 144, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, SpecularColorPower) == 208, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, FogColor) == 224, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, FogVector) == 240, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DLightingConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// DX-64: matches alpha_test3d.vert.hlsl / alpha_test3d.frag.hlsl's shared
    /// `cbuffer PerDraw : register(b0)` byte-for-byte (128 bytes). A dedicated struct, not a reuse
    /// of D3DPerDrawConstants above -- alpha_test3d's HLSL declares a genuinely different field set
    /// (AlphaRef/AlphaTol/AlphaPassW/AlphaFailW instead of Ambient/Lighting/Light0, and folds fog
    /// directly into this single buffer instead of a separate FogParams cbuffer, since
    /// alpha_test3d.glsl's own single push-constant block already had no second UBO to forward fog
    /// through -- DX-13-hlsl ported that shape as-is).
    struct alignas(16) D3DAlphaTestConstants
    {
        float Mvp[16];              ///< offset 0 (64 bytes), row_major -- see D3DPerDrawConstants::Mvp.
        float DiffuseColor[4];      ///< offset 64
        float AlphaRef;             ///< offset 80: reference alpha value
        float AlphaTol;             ///< offset 84: tolerance (>0 = equality test, 0 = comparison)
        float AlphaPassW;           ///< offset 88: weight when test passes (<0 = discard)
        float AlphaFailW;           ///< offset 92: weight when test fails  (<0 = discard)
        float FogVector[4];         ///< offset 96: REMED-GFX-005/010 FNA view-space fog vector (dotted
                                     ///< with float4(pos,1) in the VS). Replaces the former scalar
                                     ///< VertexColorEnabled/FogEnabled/FogStart/FogEnd quartet. All-zero = no fog.
        float FogColor[3];          ///< offset 112
        float VertexColorEnabled;   ///< offset 124: moved here from 96 (only alpha_test_colored3d's VS reads it)
    };
    static_assert(sizeof(D3DAlphaTestConstants) == 128, "D3DAlphaTestConstants must match alpha_test3d's real 128-byte HLSL cbuffer size");
    static_assert(offsetof(D3DAlphaTestConstants, DiffuseColor) == 64, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaRef) == 80, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaTol) == 84, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaPassW) == 88, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaFailW) == 92, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, FogVector) == 96, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, FogColor) == 112, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, VertexColorEnabled) == 124, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DAlphaTestConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// DX-60a: matches skinned3d.vert.hlsl's `cbuffer BoneBlock : register(b1)` byte-for-byte --
    /// the SkinnedEffect 72-bone array, kept as its OWN separate constant buffer rather than folded
    /// into D3DPerDrawConstants/D3DLightingConstants, so the common per-draw buffer's size and
    /// update frequency stay independent of whether a given draw uses skinning at all. NOT YET
    /// WIRED into any draw call -- ahead of DX-67 (skinned3d pipeline wiring).
    struct alignas(16) D3DBoneConstants
    {
        float Bones[72][16];   ///< 72 row_major float4x4 matrices, offset 0 (4608 bytes total)
    };
    static_assert(sizeof(D3DBoneConstants) == 72 * 64, "D3DBoneConstants must match BoneBlock's real 4608-byte HLSL cbuffer size (72 mat4)");
    static_assert(sizeof(D3DBoneConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// DX-67: matches skinned3d.vert.hlsl / skinned3d.frag.hlsl's shared
    /// `cbuffer FogParams : register(b2)` byte-for-byte (240 bytes). Named "extra" rather than
    /// "fog" since fog is only the first 32 bytes -- BoneBlock (b1) leaves this cbuffer to also
    /// carry DirectionalLight1/2, the World matrix, EyePosition, and per-light specular (the
    /// 128-byte PerDraw buffer has no spare room, same reasoning D3DLightingConstants documents
    /// for lit_textured3d).
    struct alignas(16) D3DSkinnedExtraConstants
    {
        float FogColor[4];            ///< offset 0:   xyz = FogColor, w = reserved padding
        float FogVector[4];           ///< offset 16:  CPU-prepared FNA view-space fog vector
        float Light1Dir[4];            ///< offset 32:  xyz + pad
        float Light1Diffuse[4];        ///< offset 48
        float Light2Dir[4];            ///< offset 64
        float Light2Diffuse[4];        ///< offset 80
        float World[16];               ///< offset 96:  row_major float4x4 (64 bytes)
        float EyePosition[4];          ///< offset 160: xyz, w = WeightsPerVertex (1/2/4, DX-67/Task 895)
        float SpecularColorPower[4];   ///< offset 176: xyz = SpecularColor, w = SpecularPower
        float Light0Specular[4];       ///< offset 192
        float Light1Specular[4];       ///< offset 208
        float Light2Specular[4];       ///< offset 224
        float EmissiveColor[4];        ///< offset 240: REMED-GFX-008 pre-folded (emissive + ambient*diffuse)*alpha
    };
    static_assert(sizeof(D3DSkinnedExtraConstants) == 256, "D3DSkinnedExtraConstants must match skinned3d's real 256-byte FogParams cbuffer size");
    static_assert(offsetof(D3DSkinnedExtraConstants, FogColor) == 0, "D3DSkinnedExtraConstants FogColor offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, FogVector) == 16, "D3DSkinnedExtraConstants FogVector offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, EmissiveColor) == 240, "D3DSkinnedExtraConstants EmissiveColor offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, Light1Dir) == 32, "D3DSkinnedExtraConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, World) == 96, "D3DSkinnedExtraConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, EyePosition) == 160, "D3DSkinnedExtraConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, SpecularColorPower) == 176, "D3DSkinnedExtraConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DSkinnedExtraConstants, Light0Specular) == 192, "D3DSkinnedExtraConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DSkinnedExtraConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// DX-66: matches env_map3d.vert.hlsl's `cbuffer PerDraw : register(b0)` byte-for-byte (128
    /// bytes) -- a genuinely different shape from D3DPerDrawConstants (just Mvp + World, no
    /// material/lighting fields -- those live in D3DEnvMapConstants below at register(b2) instead).
    struct alignas(16) D3DEnvMapPerDrawConstants
    {
        float Mvp[16];    ///< row_major float4x4, offset 0 (64 bytes).
        float World[16];  ///< row_major float4x4, offset 64 (64 bytes).
    };
    static_assert(sizeof(D3DEnvMapPerDrawConstants) == 128, "D3DEnvMapPerDrawConstants must match env_map3d's real 128-byte PerDraw cbuffer size");
    static_assert(offsetof(D3DEnvMapPerDrawConstants, World) == 64, "D3DEnvMapPerDrawConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DEnvMapPerDrawConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// DX-66: matches env_map3d.vert.hlsl / env_map3d.frag.hlsl's shared
    /// `cbuffer EnvMapParams : register(b2)` byte-for-byte (192 bytes) -- field names below use the
    /// vertex-shader-side naming; the pixel shader reinterprets the same bytes with its own field
    /// names (Light0DiffFresnelEn.w = fresnelEnabled, EnvMapSpecFresnelF.w = fresnelFactor), same
    /// underlying layout either way.
    struct alignas(16) D3DEnvMapConstants
    {
        float EyePosition[4];        ///< offset 0:   xyz + pad
        float DiffuseColor[4];       ///< offset 16
        float EmissiveAmount[4];     ///< offset 32:  xyz = EmissiveColor, w = envMapAmount
        float Light0Dir[4];          ///< offset 48:  xyz + pad
        float Light0DiffuseFresnel[4]; ///< offset 64: xyz = Light0Diffuse, w = fresnelEnabled (0/1)
        float EnvMapSpecularFresnel[4]; ///< offset 80: xyz = envMapSpecular, w = fresnelFactor
        float FogColor[4];          ///< offset 96:  xyz = FogColor, w = reserved padding
        float FogVector[4];         ///< offset 112: CPU-prepared FNA view-space fog vector
        float Light1Dir[4];          ///< offset 128: xyz + pad
        float Light1Diffuse[4];      ///< offset 144
        float Light2Dir[4];          ///< offset 160: xyz + pad
        float Light2Diffuse[4];      ///< offset 176
    };
    static_assert(sizeof(D3DEnvMapConstants) == 192, "D3DEnvMapConstants must match EnvMapParams's real 192-byte HLSL cbuffer size");
    static_assert(offsetof(D3DEnvMapConstants, EmissiveAmount) == 32, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DEnvMapConstants, FogColor) == 96, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DEnvMapConstants, FogVector) == 112, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DEnvMapConstants, Light1Dir) == 128, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DEnvMapConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// DX-70: matches sprite2d.vert.hlsl's `cbuffer PerDraw : register(b0)` byte-for-byte -- just a
    /// `float2 ViewportSize` (8 bytes), padded out to the required 16-byte buffer-size multiple.
    /// SpriteBatch's own quad-batching renderer is this struct's only consumer.
    struct alignas(16) D3DSprite2DConstants
    {
        float ViewportSize[2];   ///< offset 0
        float _Pad[2];           ///< offset 8 (unread by the shader, pads ByteWidth to 16)
    };
    static_assert(sizeof(D3DSprite2DConstants) == 16, "D3DSprite2DConstants must be a 16-byte buffer (sprite2d's real PerDraw cbuffer is only 8 bytes of shader-visible data)");
    static_assert(sizeof(D3DSprite2DConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// plan_cnj.md CNB-58 follow-up: matches pbr3d.vert.hlsl/pbr3d.frag.hlsl's and
    /// pbr_skinned3d.vert.hlsl/pbr_skinned3d.frag.hlsl's shared `cbuffer PerDraw : register(b0)`
    /// byte-for-byte (192 bytes) -- the metallic-roughness BRDF's material-level constants
    /// (PbrEffect/SkinnedPbrEffect). Includes World (unlike D3DPerDrawConstants) since the PBR
    /// fragment stage needs a true world-space position/normal for its BRDF, same reasoning
    /// D3DLightingConstants documents for lit_textured3d.
    struct alignas(16) D3DPbrPerDrawConstants
    {
        float Mvp[16];              ///< row_major float4x4, offset 0 (64 bytes).
        float World[16];            ///< row_major float4x4, offset 64 (64 bytes).
        float DiffuseColor[4];      ///< offset 128: material base color factor (RGBA)
        float AmbientMetallic[4];   ///< offset 144: xyz = AmbientColor, w = MetallicFactor
        float EmissiveRoughness[4]; ///< offset 160: xyz = EmissiveColor, w = RoughnessFactor
        float AlphaTest[4];          ///< offset 176: reference, tolerance, pass/fail weights
    };
    static_assert(sizeof(D3DPbrPerDrawConstants) == 192, "D3DPbrPerDrawConstants must match pbr3d's real 192-byte PerDraw cbuffer size");
    static_assert(offsetof(D3DPbrPerDrawConstants, World) == 64, "D3DPbrPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPbrPerDrawConstants, DiffuseColor) == 128, "D3DPbrPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPbrPerDrawConstants, AmbientMetallic) == 144, "D3DPbrPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPbrPerDrawConstants, EmissiveRoughness) == 160, "D3DPbrPerDrawConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPbrPerDrawConstants, AlphaTest) == 176, "D3DPbrPerDrawConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DPbrPerDrawConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");

    /// plan_cnj.md CNB-58 follow-up: matches pbr3d.vert.hlsl/pbr3d.frag.hlsl's shared
    /// `cbuffer PbrLights : register(b1)` (unskinned Pbr3d) and pbr_skinned3d.vert.hlsl/
    /// pbr_skinned3d.frag.hlsl's `cbuffer PbrLights : register(b2)` (PbrSkinned3d, since BoneBlock
    /// claims b1 there) byte-for-byte (144 bytes) either way -- the same struct, only the bind
    /// slot differs per variant. EyePosWeights.w carries WeightsPerVertex (Task 895 convention,
    /// unused/0 for the unskinned Pbr3d variant, same as D3DSkinnedExtraConstants::EyePosition.w).
    struct alignas(16) D3DPbrLightConstants
    {
        float EyePosWeights[4];     ///< offset 0:   xyz = EyePosition, w = WeightsPerVertex
        float Light0Dir[4];         ///< offset 16:  xyz + pad
        float Light0Diffuse[4];     ///< offset 32
        float Light1Dir[4];         ///< offset 48:  xyz + pad
        float Light1Diffuse[4];     ///< offset 64
        float Light2Dir[4];         ///< offset 80:  xyz + pad
        float Light2Diffuse[4];     ///< offset 96
        float FogColor[4];          ///< offset 112: xyz = FogColor, w = reserved padding
        float FogVector[4];         ///< offset 128: CPU-prepared FNA view-space fog vector
    };
    static_assert(sizeof(D3DPbrLightConstants) == 144, "D3DPbrLightConstants must match PbrLights's real 144-byte HLSL cbuffer size");
    static_assert(offsetof(D3DPbrLightConstants, Light0Dir) == 16, "D3DPbrLightConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPbrLightConstants, FogColor) == 112, "D3DPbrLightConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DPbrLightConstants, FogVector) == 128, "D3DPbrLightConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DPbrLightConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");
}
