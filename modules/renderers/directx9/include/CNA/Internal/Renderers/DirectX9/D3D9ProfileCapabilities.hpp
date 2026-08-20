#pragma once

// plans/plan_dx9.md Phase D9-10 (D9-100/D9-101/D9-102/D9-103): XNA 4.0's GraphicsProfile.Reach/HiDef
// capability floors, real on this renderer only (the other 9 CNA renderers have no D3DCAPS9 to
// query and keep their honest GraphicsAdapter::IsProfileSupported() == true; -- see plans/plan_dx9.md's
// own "Boundaries" section). See plans/plan_dx9.md's D9-100 closure note for the full researched table
// and its citations (FNA's own ProfileCapabilities.cs -- confirmed dead code, data-only, never
// actually consulted by FNA's own resource-creation/validation paths -- cross-checked against
// Shawn Hargreaves' official XNA team blog post, "Reach vs HiDef").
//
// GraphicsProfile ordinals below match Microsoft::Xna::Framework::Graphics::GraphicsProfile
// exactly (Reach=0, HiDef=1) -- plain int, matching this renderer's own established convention
// for XNA enum interop (D3D9StateMapping.hpp).
//
// D9-105's own honest caveat, restated here (not just in plans/plan_dx9.md) so it can't be missed by
// reading code alone: every D3DCAPS9-consuming function below is REAL logic against a REAL
// IDirect3D9/IDirect3DDevice9 -- but in this project's Wine+DXVK dev loop, the D3DCAPS9 VALUES
// those calls return are DXVK's own synthesized capability set, not what an authentic XNA-era
// (~2006-2013) Direct3D 9 driver would report. IsProfileSupported(HiDef) returning true here
// proves the comparison logic is correct, NOT that a real HiDef-class GPU exists in this loop --
// it doesn't; there is no such hardware here. Provisional until D9-140 (real Windows hardware,
// needs_human). Do not cite this dev environment's own results as proof of hardware authenticity.

#include <d3d9.h>

namespace CNA::Internal::Renderers::DirectX9
{
    /**
     * @brief Queries the real D3DCAPS9 of the default adapter's HAL device type, WITHOUT
     * creating a live IDirect3DDevice9. IDirect3D9::GetDeviceCaps() is a genuine, lightweight,
     * pre-device-creation capability probe -- the same call DirectX9Renderer's own constructor
     * already makes as `preCreateCaps` before CreateDevice() (D9-30). Returns a default-
     * constructed D3DCAPS9 (all zero) if Direct3DCreate9/GetDeviceCaps fails for any reason --
     * callers must treat an all-zero result as "no suitable device", never as success.
     */
    D3DCAPS9 QueryAdapterCapsEXT();

    /**
     * @brief Real XNA 4.0 GraphicsProfile.HiDef capability floor, checked against the given caps.
     * Reach itself has no floor worth checking on this renderer -- every real D3D9 HAL device
     * already exceeds every Reach-profile minimum (matches D9-32's own construction-time
     * finding). Checks the hardware-queryable subset of the D9-100 table: vs_3_0/ps_3_0,
     * MaxTextureWidth/Height >= 4096, MaxVolumeExtent >= 256, NumSimultaneousRTs >= 4, and
     * unconditional (non-POW2-restricted) texture addressing.
     */
    bool MeetsHiDefFloorEXT(const D3DCAPS9& caps);

    /// D9-100's own table: GraphicsProfile.Reach=2048, HiDef=4096 (Direct3D 9 SDK max is 8192,
    /// but the XNA profile itself imposes this lower ceiling regardless of what the underlying
    /// hardware could otherwise support -- the entire point of the profile system).
    int MaxTextureSizeForProfileEXT(int graphicsProfile);

    /// D9-100's own table: Reach=512, HiDef=4096.
    int MaxCubeSizeForProfileEXT(int graphicsProfile);

    /// D9-100's own table: Reach=0 (volume/3D textures unsupported at all), HiDef=256.
    int MaxVolumeExtentForProfileEXT(int graphicsProfile);

    /// D9-100's own table: Reach=1, HiDef=4 (matches D3DCAPS9::NumSimultaneousRTs' own HiDef
    /// floor, D9-54's own hardware-capped MRT enforcement uses the ACTUAL device cap; this is the
    /// separate, lower, profile-imposed ceiling for GraphicsProfile.Reach specifically).
    int MaxRenderTargetsForProfileEXT(int graphicsProfile);

    /// D9-100's own table: whether the given XNA SurfaceFormat ordinal is a valid TEXTURE format
    /// under the given profile (Reach's own 9-format whitelist vs. HiDef's 20-format superset).
    bool IsValidTextureFormatForProfileEXT(int graphicsProfile, int surfaceFormat);

    /**
     * @brief D9-102: real hardware format-support probe for GraphicsAdapter::
     * QueryRenderTargetFormat()/QueryBackBufferFormat(), via IDirect3D9::CheckDeviceFormat --
     * another genuine pre-device-creation query (no live IDirect3DDevice9 needed), gated on the
     * CURRENT adapter display mode's own format (matching real D3D9's own CheckDeviceFormat
     * contract, which always checks a candidate format against a specific adapter format).
     * Returns false if Direct3DCreate9/GetAdapterDisplayMode/CheckDeviceFormat fails for any
     * reason.
     */
    bool IsRenderTargetFormatSupportedByHardwareEXT(D3DFORMAT format);

    /**
     * @brief D9-102: real hardware BACK-BUFFER (swap-chain) format-support probe, via
     * IDirect3D9::CheckDeviceType -- the real D3D9 API for "can this format be presented
     * (displayed) against the current adapter format", distinct from CheckDeviceFormat's own
     * general-texture-usage question above (D9-30's own finding: the swap chain has display-
     * compatibility restrictions an ordinary render-target texture does not). Returns false if
     * Direct3DCreate9/GetAdapterDisplayMode/CheckDeviceType fails for any reason.
     */
    bool IsBackBufferFormatSupportedByHardwareEXT(D3DFORMAT format);

    /**
     * @brief D9-102: clamps a requested multisample count down to the nearest hardware-supported
     * value for the given format, via IDirect3D9::CheckDeviceMultiSampleType (the same real API
     * DirectX9Renderer::ClampMultiSampleCountEXT already uses at a live device's own
     * IDirect3D9, reimplemented here as a free, pre-device-creation function for
     * GraphicsAdapter's own use). Returns 0 (no multisampling) if `requested <= 1` or the probe
     * itself fails.
     */
    int ClampMultiSampleCountForFormatEXT(D3DFORMAT format, int requested);
}
