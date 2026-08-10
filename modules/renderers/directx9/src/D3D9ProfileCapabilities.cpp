// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX9/D3D9ProfileCapabilities.hpp"

#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    D3DCAPS9 QueryAdapterCapsEXT()
    {
        D3DCAPS9 caps{};

        ComPtr<IDirect3D9> d3d9;
        d3d9.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!d3d9)
            return caps;

        d3d9->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
        return caps;
    }

    bool MeetsHiDefFloorEXT(const D3DCAPS9& caps)
    {
        if (caps.VertexShaderVersion < static_cast<DWORD>(D3DVS_VERSION(3, 0))) return false;
        if (caps.PixelShaderVersion < static_cast<DWORD>(D3DPS_VERSION(3, 0))) return false;
        if (caps.MaxTextureWidth < 4096 || caps.MaxTextureHeight < 4096) return false;
        if (caps.MaxVolumeExtent < 256) return false;
        if (caps.NumSimultaneousRTs < 4) return false;
        // D9-100's own HiDef floor: unconditional (unrestricted) NPOT texture support -- POW2
        // must NOT be required at all, matching DirectX9Renderer::RequiresPowerOfTwoTexturesEXT
        // and NonPowerOfTwoRequiresClampAddressingEXT's own established TextureCaps convention.
        if (caps.TextureCaps & D3DPTEXTURECAPS_POW2) return false;

        return true;
    }

    int MaxTextureSizeForProfileEXT(int graphicsProfile)
    {
        return graphicsProfile == 1 /* GraphicsProfile::HiDef */ ? 4096 : 2048;
    }

    int MaxCubeSizeForProfileEXT(int graphicsProfile)
    {
        return graphicsProfile == 1 /* GraphicsProfile::HiDef */ ? 4096 : 512;
    }

    int MaxVolumeExtentForProfileEXT(int graphicsProfile)
    {
        return graphicsProfile == 1 /* GraphicsProfile::HiDef */ ? 256 : 0;
    }

    int MaxRenderTargetsForProfileEXT(int graphicsProfile)
    {
        return graphicsProfile == 1 /* GraphicsProfile::HiDef */ ? 4 : 1;
    }

    bool IsValidTextureFormatForProfileEXT(int graphicsProfile, int surfaceFormat)
    {
        // XNA SurfaceFormat ordinals (Microsoft::Xna::Framework::Graphics::SurfaceFormat):
        // 0 Color, 1 Bgr565, 2 Bgra5551, 3 Bgra4444, 4 Dxt1, 5 Dxt3, 6 Dxt5, 7 NormalizedByte2,
        // 8 NormalizedByte4, 9 Rgba1010102, 10 Rg32, 11 Rgba64, 12 Alpha8, 13 Single, 14 Vector2,
        // 15 Vector4, 16 HalfSingle, 17 HalfVector2, 18 HalfVector4, 19 HdrBlendable.
        //
        // D9-100's own table: Reach's 9-format whitelist (0-8) is a strict subset of HiDef's
        // 20-format superset (0-19) -- every CNAEXT extension format (ColorBgraEXT and later,
        // ordinal >= 20) is CNA-specific and out of XNA's own profile table entirely, treated as
        // HiDef-only (never part of any real XNA profile, so never "Reach-valid").
        if (surfaceFormat < 0) return false;
        if (graphicsProfile == 1 /* GraphicsProfile::HiDef */) return surfaceFormat <= 19;
        return surfaceFormat <= 8;
    }

    bool IsRenderTargetFormatSupportedByHardwareEXT(D3DFORMAT format)
    {
        ComPtr<IDirect3D9> d3d9;
        d3d9.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!d3d9)
            return false;

        D3DDISPLAYMODE mode{};
        if (FAILED(d3d9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
            return false;

        const HRESULT hr = d3d9->CheckDeviceFormat(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
            D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, format);
        return SUCCEEDED(hr);
    }

    bool IsBackBufferFormatSupportedByHardwareEXT(D3DFORMAT format)
    {
        ComPtr<IDirect3D9> d3d9;
        d3d9.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!d3d9)
            return false;

        D3DDISPLAYMODE mode{};
        if (FAILED(d3d9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
            return false;

        const HRESULT hr = d3d9->CheckDeviceType(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format, format, TRUE);
        return SUCCEEDED(hr);
    }

    int ClampMultiSampleCountForFormatEXT(D3DFORMAT format, int requested)
    {
        if (requested <= 1) return 0;

        ComPtr<IDirect3D9> d3d9;
        d3d9.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!d3d9)
            return 0;

        // D3D9's D3DMULTISAMPLE_TYPE enumerators for 2..16 samples are numerically equal to the
        // sample count itself (matches DirectX9Renderer::ClampMultiSampleCountEXT's own
        // established convention -- no lookup table needed).
        DWORD qualityLevels = 0;
        const HRESULT hr = d3d9->CheckDeviceMultiSampleType(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, format, TRUE,
            static_cast<D3DMULTISAMPLE_TYPE>(requested), &qualityLevels);
        return SUCCEEDED(hr) ? requested : 0;
    }
}
