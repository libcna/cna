#pragma once

// plan_dx.md Phase DX8 (DX-60/DX-60a): explicit GPU-side constant-buffer POD structs matching the
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

namespace CNA::Internal::Backends::D3DCommon
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
        float FogColorEnabled[4];   ///< offset 0: xyz = FogColor, w = fogEnabled (>0.5 = enabled)
        float FogStartEnd[4];       ///< offset 16: x = fogStart, y = fogEnd, zw = unused
    };
    static_assert(sizeof(D3DFogConstants) == 32, "D3DFogConstants must match FogParams's real 32-byte HLSL cbuffer size");
    static_assert(offsetof(D3DFogConstants, FogStartEnd) == 16, "D3DFogConstants field offset mismatch vs HLSL");
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
        float FogColorEnabled[4];      ///< offset 224: xyz = FogColor, w = fogEnabled
        float FogStartEnd[4];          ///< offset 240: x = fogStart, y = fogEnd, zw = unused
    };
    static_assert(sizeof(D3DLightingConstants) == 256, "D3DLightingConstants must match LitLightParams's real 256-byte HLSL cbuffer size");
    static_assert(offsetof(D3DLightingConstants, World) == 80, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, EyePosition) == 144, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, SpecularColorPower) == 208, "D3DLightingConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DLightingConstants, FogColorEnabled) == 224, "D3DLightingConstants field offset mismatch vs HLSL");
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
        float VertexColorEnabled;   ///< offset 96 (unused by alpha_test3d's VSInput -- no color attribute)
        float FogEnabled;           ///< offset 100
        float FogStart;             ///< offset 104
        float FogEnd;               ///< offset 108
        float FogColor[3];          ///< offset 112
    };
    static_assert(sizeof(D3DAlphaTestConstants) == 128, "D3DAlphaTestConstants must match alpha_test3d's real 128-byte HLSL cbuffer size");
    static_assert(offsetof(D3DAlphaTestConstants, DiffuseColor) == 64, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaRef) == 80, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaTol) == 84, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaPassW) == 88, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, AlphaFailW) == 92, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, VertexColorEnabled) == 96, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, FogEnabled) == 100, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, FogStart) == 104, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, FogEnd) == 108, "D3DAlphaTestConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DAlphaTestConstants, FogColor) == 112, "D3DAlphaTestConstants field offset mismatch vs HLSL");
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
        float FogColorEnabled[4];      ///< offset 0:   xyz = FogColor, w = fogEnabled
        float FogStartEnd[4];          ///< offset 16:  x = fogStart, y = fogEnd, zw = unused
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
    };
    static_assert(sizeof(D3DSkinnedExtraConstants) == 240, "D3DSkinnedExtraConstants must match skinned3d's real 240-byte FogParams cbuffer size");
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
        float FogColorEnabled[4];    ///< offset 96:  xyz = FogColor, w = fogEnabled
        float FogStartEnd[4];        ///< offset 112: x = fogStart, y = fogEnd, zw = unused
        float Light1Dir[4];          ///< offset 128: xyz + pad
        float Light1Diffuse[4];      ///< offset 144
        float Light2Dir[4];          ///< offset 160: xyz + pad
        float Light2Diffuse[4];      ///< offset 176
    };
    static_assert(sizeof(D3DEnvMapConstants) == 192, "D3DEnvMapConstants must match EnvMapParams's real 192-byte HLSL cbuffer size");
    static_assert(offsetof(D3DEnvMapConstants, EmissiveAmount) == 32, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DEnvMapConstants, FogColorEnabled) == 96, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(offsetof(D3DEnvMapConstants, Light1Dir) == 128, "D3DEnvMapConstants field offset mismatch vs HLSL");
    static_assert(sizeof(D3DEnvMapConstants) % 16 == 0, "D3D11 constant buffer ByteWidth must be a 16-byte multiple");
}
