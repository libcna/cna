// plans/plan_dx9.md Phase D9-2 (D9-20).
#include "CNA/Internal/Renderers/DirectX9/D3D9FormatMapping.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;

    D3DFORMAT SurfaceFormatToD3D9(int surfaceFormat)
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            // XNA's Color is byte order R,G,B,A in memory (CLAUDE.md: Color's packed layout is
            // AABBGGRR as a uint32, i.e. R at the lowest byte). D3D9's channel-order names read
            // MSB-to-LSB (opposite DXGI's LSB-to-MSB convention) -- verified against Microsoft's
            // own D3D9-to-D3D10 legacy-format table: D3DFMT_A8B8G8R8 <-> DXGI_FORMAT_R8G8B8A8_UNORM
            // (the same R,G,B,A memory layout D3D11's own SurfaceFormatToDxgi() maps Color to).
            case SurfaceFormat::Color:            return D3DFMT_A8B8G8R8;
            case SurfaceFormat::Bgr565:            return D3DFMT_R5G6B5;
            case SurfaceFormat::Bgra5551:          return D3DFMT_A1R5G5B5;
            case SurfaceFormat::Bgra4444:          return D3DFMT_A4R4G4B4;
            case SurfaceFormat::Dxt1:               return D3DFMT_DXT1;
            case SurfaceFormat::Dxt3:               return D3DFMT_DXT3;
            case SurfaceFormat::Dxt5:               return D3DFMT_DXT5;
            // Legacy D3D9 bump-mapping formats -- U/V/W/Q are the historical per-channel names,
            // not R/G/B/A, but the byte layout matches R8G8(_SNORM)/R8G8B8A8(_SNORM) exactly
            // (Microsoft's own legacy-format table: D3DFMT_V8U8 <-> DXGI_FORMAT_R8G8_SNORM,
            // D3DFMT_Q8W8V8U8 <-> DXGI_FORMAT_R8G8B8A8_SNORM).
            case SurfaceFormat::NormalizedByte2:   return D3DFMT_V8U8;
            case SurfaceFormat::NormalizedByte4:   return D3DFMT_Q8W8V8U8;
            // NOT D3DFMT_A2R10G10B10 -- that format's bits (A high, then R, G, B low) do not match
            // DXGI_FORMAT_R10G10B10A2_UNORM's layout (R low, then G, B, A high); Microsoft's own
            // legacy-format table lists D3DFMT_A2R10G10B10 as having "no DXGI equivalent" for
            // exactly this reason. D3DFMT_A2B10G10R10 is the one whose bits actually match (R in
            // the low 10 bits, A in the high 2) -- verified by deriving both formats' bit layout
            // from their own channel-order names, not assumed from the superficial name similarity.
            case SurfaceFormat::Rgba1010102:      return D3DFMT_A2B10G10R10;
            case SurfaceFormat::Rg32:               return D3DFMT_G16R16;
            case SurfaceFormat::Rgba64:             return D3DFMT_A16B16G16R16;
            case SurfaceFormat::Alpha8:             return D3DFMT_A8;
            case SurfaceFormat::Single:              return D3DFMT_R32F;
            case SurfaceFormat::Vector2:            return D3DFMT_G32R32F;
            case SurfaceFormat::Vector4:            return D3DFMT_A32B32G32R32F;
            case SurfaceFormat::HalfSingle:         return D3DFMT_R16F;
            case SurfaceFormat::HalfVector2:        return D3DFMT_G16R16F;
            case SurfaceFormat::HalfVector4:        return D3DFMT_A16B16G16R16F;
            case SurfaceFormat::HdrBlendable:       return D3DFMT_A16B16G16R16F;
            // ColorBgraEXT is memory byte order B,G,R,A -- D3DFMT_A8R8G8B8 (Microsoft's own
            // legacy-format table: D3DFMT_A8R8G8B8 <-> DXGI_FORMAT_B8G8R8A8_UNORM), the reverse of
            // Color's own D3DFMT_A8B8G8R8 above.
            case SurfaceFormat::ColorBgraEXT:       return D3DFMT_A8R8G8B8;
            // D3D9 has no distinct sRGB format constants at all -- sRGB decoding is a per-sampler
            // render state (D3DSAMP_SRGBTEXTURE), not a format bit, unlike D3D11's dedicated
            // _SRGB DXGI_FORMAT values. These two therefore intentionally return the SAME
            // D3DFORMAT as their non-sRGB counterpart; D9-63 (ApplySamplerState) is responsible
            // for setting D3DSAMP_SRGBTEXTURE when sampling a texture created with one of these.
            case SurfaceFormat::ColorSrgbEXT:       return D3DFMT_A8B8G8R8;
            case SurfaceFormat::Dxt5SrgbEXT:        return D3DFMT_DXT5;
            // BC7 postdates D3D9 entirely (introduced with D3D11) -- genuinely no equivalent,
            // not silently substituted with a lookalike compressed format.
            case SurfaceFormat::Bc7EXT:              return D3DFMT_UNKNOWN;
            case SurfaceFormat::Bc7SrgbEXT:          return D3DFMT_UNKNOWN;
            // D3D9 has no plain single-channel "R8"/"R16" format name -- the historical
            // single-channel formats are named for luminance instead (Microsoft's own
            // legacy-format table: D3DFMT_L8 <-> DXGI_FORMAT_R8_UNORM, D3DFMT_L16 <->
            // DXGI_FORMAT_R16_UNORM), but the byte layout is identical for a single-channel format.
            case SurfaceFormat::ByteEXT:            return D3DFMT_L8;
            case SurfaceFormat::UShortEXT:          return D3DFMT_L16;
            default:                                  return D3DFMT_UNKNOWN;
        }
    }

    D3DFORMAT DepthFormatToD3D9(int depthFormat)
    {
        switch (static_cast<DepthFormat>(depthFormat))
        {
            case DepthFormat::None:             return D3DFMT_UNKNOWN;
            case DepthFormat::Depth16:          return D3DFMT_D16;
            // Unlike D3D11 (no pure-depth 24-bit format, DX-11-fmt folds this into D24S8), D3D9
            // has a real depth-only 24-bit format.
            case DepthFormat::Depth24:          return D3DFMT_D24X8;
            case DepthFormat::Depth24Stencil8:  return D3DFMT_D24S8;
            default:                              return D3DFMT_UNKNOWN;
        }
    }
}
