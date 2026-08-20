// plans/plan_dx.md Phase DIRECTX3 (DX-11-fmt).
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

namespace CNA::Internal::Renderers::D3DCommon
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;

    DXGI_FORMAT SurfaceFormatToDxgi(int surfaceFormat)
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:            return DXGI_FORMAT_R8G8B8A8_UNORM;
            case SurfaceFormat::Bgr565:           return DXGI_FORMAT_B5G6R5_UNORM;
            case SurfaceFormat::Bgra5551:         return DXGI_FORMAT_B5G5R5A1_UNORM;
            case SurfaceFormat::Bgra4444:         return DXGI_FORMAT_B4G4R4A4_UNORM;
            case SurfaceFormat::Dxt1:              return DXGI_FORMAT_BC1_UNORM;
            case SurfaceFormat::Dxt3:              return DXGI_FORMAT_BC2_UNORM;
            case SurfaceFormat::Dxt5:              return DXGI_FORMAT_BC3_UNORM;
            case SurfaceFormat::NormalizedByte2:  return DXGI_FORMAT_R8G8_SNORM;
            case SurfaceFormat::NormalizedByte4:  return DXGI_FORMAT_R8G8B8A8_SNORM;
            case SurfaceFormat::Rgba1010102:      return DXGI_FORMAT_R10G10B10A2_UNORM;
            case SurfaceFormat::Rg32:              return DXGI_FORMAT_R16G16_UNORM;
            case SurfaceFormat::Rgba64:            return DXGI_FORMAT_R16G16B16A16_UNORM;
            case SurfaceFormat::Alpha8:            return DXGI_FORMAT_A8_UNORM;
            case SurfaceFormat::Single:             return DXGI_FORMAT_R32_FLOAT;
            case SurfaceFormat::Vector2:           return DXGI_FORMAT_R32G32_FLOAT;
            case SurfaceFormat::Vector4:           return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case SurfaceFormat::HalfSingle:        return DXGI_FORMAT_R16_FLOAT;
            case SurfaceFormat::HalfVector2:       return DXGI_FORMAT_R16G16_FLOAT;
            case SurfaceFormat::HalfVector4:       return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case SurfaceFormat::HdrBlendable:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case SurfaceFormat::ColorBgraEXT:      return DXGI_FORMAT_B8G8R8A8_UNORM;
            case SurfaceFormat::ColorSrgbEXT:      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case SurfaceFormat::Dxt5SrgbEXT:       return DXGI_FORMAT_BC3_UNORM_SRGB;
            case SurfaceFormat::Bc7EXT:            return DXGI_FORMAT_BC7_UNORM;
            case SurfaceFormat::Bc7SrgbEXT:        return DXGI_FORMAT_BC7_UNORM_SRGB;
            case SurfaceFormat::ByteEXT:           return DXGI_FORMAT_R8_UNORM;
            case SurfaceFormat::UShortEXT:         return DXGI_FORMAT_R16_UNORM;
            default:                                return DXGI_FORMAT_UNKNOWN;
        }
    }

    DXGI_FORMAT DepthFormatToDxgi(int depthFormat)
    {
        switch (static_cast<DepthFormat>(depthFormat))
        {
            case DepthFormat::None:             return DXGI_FORMAT_UNKNOWN;
            case DepthFormat::Depth16:          return DXGI_FORMAT_D16_UNORM;
            // D3D11 has no pure 24-bit depth-only format -- Depth24 and Depth24Stencil8 both map
            // to the same combined format, matching this project's own Vulkan renderer's fallback.
            case DepthFormat::Depth24:          return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case DepthFormat::Depth24Stencil8:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
            default:                             return DXGI_FORMAT_UNKNOWN;
        }
    }
}
