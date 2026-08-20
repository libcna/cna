#pragma once

// plans/plan_dx9.md Phase D9-2 (D9-20): XNA SurfaceFormat/DepthFormat -> D3DFORMAT mapping.
//
// design decision 12: D3D9 does NOT share D3DCommon's own D3DFormatMapping (DXGI_FORMAT is a
// different enum space, and D3D9's channel-order naming convention is the historical reverse of
// DXGI's -- see this file's own D3DFMT_A8B8G8R8/D3DFMT_A8R8G8B8 handling below). This is its own,
// independently-verified table (cross-checked against Microsoft's own D3D9-to-D3D10/DXGI legacy
// format mapping documentation, not derived by analogy with D3D11's choices).

#include <d3d9.h>

namespace CNA::Internal::Renderers::DirectX9
{
    /// Maps an XNA SurfaceFormat ordinal (Microsoft::Xna::Framework::Graphics::SurfaceFormat cast
    /// to int, matching D3DCommon's own convention) to the corresponding D3DFORMAT. Returns
    /// D3DFMT_UNKNOWN for an unrecognized ordinal, or for an XNA format with genuinely no D3D9
    /// equivalent (Bc7EXT/Bc7SrgbEXT -- BC7 postdates D3D9 entirely) -- never silently substituted
    /// with a lookalike format.
    D3DFORMAT SurfaceFormatToD3D9(int surfaceFormat);

    /// Maps an XNA DepthFormat ordinal (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3) to the
    /// corresponding D3DFORMAT. Unlike D3D11 (which has no pure 24-bit-depth-only format and folds
    /// Depth24 into the combined D24S8 format), D3D9 has a real depth-only 24-bit format
    /// (D3DFMT_D24X8) -- Depth24 maps to that, not to D3DFMT_D24S8. Returns D3DFMT_UNKNOWN for
    /// None (no depth buffer requested) or an unrecognized ordinal.
    D3DFORMAT DepthFormatToD3D9(int depthFormat);
}
