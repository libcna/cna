#pragma once

// plans/plan_dx.md Phase DIRECTX3 (DX-11-fmt): XNA SurfaceFormat/DepthFormat -> DXGI_FORMAT mapping, shared
// between D3D11 and D3D12 (design decision 4). Cross-checked against the equivalent XNA/FNA/D3D
// format correspondences used by every other renderer's own format table.

#include <dxgiformat.h>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// Maps an XNA SurfaceFormat ordinal (Microsoft::Xna::Framework::Graphics::SurfaceFormat cast
    /// to int, to avoid coupling this shared header to the XNA namespace -- mirrors
    /// CreateTexture3D/CreateTextureCube's own `surfaceFormat` int convention in IGraphicsRenderer)
    /// to the corresponding DXGI_FORMAT. Returns DXGI_FORMAT_UNKNOWN for an unrecognized ordinal.
    DXGI_FORMAT SurfaceFormatToDxgi(int surfaceFormat);

    /// Maps an XNA DepthFormat ordinal (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3) to the
    /// corresponding DXGI_FORMAT. D3D11 has no pure 24-bit-depth-only format, so Depth24 maps to
    /// the same DXGI_FORMAT_D24_UNORM_S8_UINT as Depth24Stencil8 -- matches this project's own
    /// Vulkan renderer, which falls back to the same combined format when a depth-only format is
    /// requested (see VulkanRenderer.cpp's own depth-format candidate list). Returns
    /// DXGI_FORMAT_UNKNOWN for None (no depth buffer requested) or an unrecognized ordinal.
    DXGI_FORMAT DepthFormatToDxgi(int depthFormat);
}
