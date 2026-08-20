// plans/plan_dx9.md Phase D9-2 (D9-21).
#include "CNA/Internal/Renderers/DirectX9/D3D9StateMapping.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

namespace CNA::Internal::Renderers::DirectX9
{
    using namespace Microsoft::Xna::Framework::Graphics;

    D3DBLEND BlendToD3D9(int blend)
    {
        switch (static_cast<Blend>(blend))
        {
            case Blend::One:                     return D3DBLEND_ONE;
            case Blend::Zero:                     return D3DBLEND_ZERO;
            case Blend::SourceColor:             return D3DBLEND_SRCCOLOR;
            case Blend::InverseSourceColor:      return D3DBLEND_INVSRCCOLOR;
            case Blend::SourceAlpha:             return D3DBLEND_SRCALPHA;
            case Blend::InverseSourceAlpha:      return D3DBLEND_INVSRCALPHA;
            case Blend::DestinationColor:        return D3DBLEND_DESTCOLOR;
            case Blend::InverseDestinationColor: return D3DBLEND_INVDESTCOLOR;
            case Blend::DestinationAlpha:        return D3DBLEND_DESTALPHA;
            case Blend::InverseDestinationAlpha: return D3DBLEND_INVDESTALPHA;
            case Blend::BlendFactor:             return D3DBLEND_BLENDFACTOR;
            case Blend::InverseBlendFactor:      return D3DBLEND_INVBLENDFACTOR;
            case Blend::SourceAlphaSaturation:   return D3DBLEND_SRCALPHASAT;
            default:                               return D3DBLEND_ONE;
        }
    }

    D3DBLENDOP BlendFunctionToD3D9(int blendFunction)
    {
        switch (static_cast<BlendFunction>(blendFunction))
        {
            case BlendFunction::Add:             return D3DBLENDOP_ADD;
            case BlendFunction::Subtract:        return D3DBLENDOP_SUBTRACT;
            case BlendFunction::ReverseSubtract: return D3DBLENDOP_REVSUBTRACT;
            case BlendFunction::Max:              return D3DBLENDOP_MAX;
            case BlendFunction::Min:              return D3DBLENDOP_MIN;
            default:                                return D3DBLENDOP_ADD;
        }
    }

    D3DCMPFUNC CompareFunctionToD3D9(int compareFunction)
    {
        switch (static_cast<CompareFunction>(compareFunction))
        {
            case CompareFunction::Always:        return D3DCMP_ALWAYS;
            case CompareFunction::Never:         return D3DCMP_NEVER;
            case CompareFunction::Less:          return D3DCMP_LESS;
            case CompareFunction::LessEqual:     return D3DCMP_LESSEQUAL;
            case CompareFunction::Equal:         return D3DCMP_EQUAL;
            case CompareFunction::GreaterEqual:  return D3DCMP_GREATEREQUAL;
            case CompareFunction::Greater:       return D3DCMP_GREATER;
            case CompareFunction::NotEqual:      return D3DCMP_NOTEQUAL;
            default:                               return D3DCMP_ALWAYS;
        }
    }

    D3DCULL CullModeToD3D9(int cullMode)
    {
        // See this function's own header doc: D3D9 defines clockwise as front-facing by default,
        // same convention D3DCommon::CullModeToD3D11 assumes for D3D11.
        switch (static_cast<CullMode>(cullMode))
        {
            case CullMode::None:                     return D3DCULL_NONE;
            case CullMode::CullClockwiseFace:        return D3DCULL_CW;
            case CullMode::CullCounterClockwiseFace: return D3DCULL_CCW;
            default:                                   return D3DCULL_NONE;
        }
    }

    D3DFILLMODE FillModeToD3D9(int fillMode)
    {
        switch (static_cast<FillMode>(fillMode))
        {
            case FillMode::Solid:      return D3DFILL_SOLID;
            case FillMode::WireFrame:  return D3DFILL_WIREFRAME;
            default:                     return D3DFILL_SOLID;
        }
    }

    D3DTEXTUREADDRESS TextureAddressModeToD3D9(int addressMode)
    {
        switch (static_cast<TextureAddressMode>(addressMode))
        {
            case TextureAddressMode::Wrap:   return D3DTADDRESS_WRAP;
            case TextureAddressMode::Clamp:  return D3DTADDRESS_CLAMP;
            case TextureAddressMode::Mirror: return D3DTADDRESS_MIRROR;
            default:                           return D3DTADDRESS_WRAP;
        }
    }

    D3DSTENCILOP StencilOperationToD3D9(int stencilOperation)
    {
        switch (static_cast<StencilOperation>(stencilOperation))
        {
            case StencilOperation::Keep:                 return D3DSTENCILOP_KEEP;
            case StencilOperation::Zero:                 return D3DSTENCILOP_ZERO;
            case StencilOperation::Replace:              return D3DSTENCILOP_REPLACE;
            case StencilOperation::Increment:            return D3DSTENCILOP_INCR;
            case StencilOperation::Decrement:            return D3DSTENCILOP_DECR;
            case StencilOperation::IncrementSaturation:  return D3DSTENCILOP_INCRSAT;
            case StencilOperation::DecrementSaturation:  return D3DSTENCILOP_DECRSAT;
            case StencilOperation::Invert:               return D3DSTENCILOP_INVERT;
            default:                                       return D3DSTENCILOP_KEEP;
        }
    }

    D3D9FilterTriple TextureFilterToD3D9(int textureFilter)
    {
        switch (static_cast<TextureFilter>(textureFilter))
        {
            case TextureFilter::Linear:
                return {D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR};
            case TextureFilter::Point:
                return {D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_POINT};
            case TextureFilter::Anisotropic:
                // D3DTEXF_ANISOTROPIC is not a legal D3DSAMP_MIPFILTER value -- see header doc.
                return {D3DTEXF_ANISOTROPIC, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR};
            case TextureFilter::LinearMipPoint:
                return {D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT};
            case TextureFilter::PointMipLinear:
                return {D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_LINEAR};
            case TextureFilter::MinLinearMagPointMipLinear:
                return {D3DTEXF_LINEAR, D3DTEXF_POINT, D3DTEXF_LINEAR};
            case TextureFilter::MinLinearMagPointMipPoint:
                return {D3DTEXF_LINEAR, D3DTEXF_POINT, D3DTEXF_POINT};
            case TextureFilter::MinPointMagLinearMipLinear:
                return {D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_LINEAR};
            case TextureFilter::MinPointMagLinearMipPoint:
                return {D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_POINT};
            default:
                return {D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR};
        }
    }
}
