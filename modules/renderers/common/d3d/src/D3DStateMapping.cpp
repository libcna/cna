// plans/plan_dx.md Phase DIRECTX3 (DX-12-state).
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

namespace CNA::Internal::Renderers::D3DCommon
{
    using namespace Microsoft::Xna::Framework::Graphics;

    D3D11_BLEND BlendToD3D11(int blend)
    {
        switch (static_cast<Blend>(blend))
        {
            case Blend::One:                     return D3D11_BLEND_ONE;
            case Blend::Zero:                     return D3D11_BLEND_ZERO;
            case Blend::SourceColor:             return D3D11_BLEND_SRC_COLOR;
            case Blend::InverseSourceColor:      return D3D11_BLEND_INV_SRC_COLOR;
            case Blend::SourceAlpha:             return D3D11_BLEND_SRC_ALPHA;
            case Blend::InverseSourceAlpha:      return D3D11_BLEND_INV_SRC_ALPHA;
            case Blend::DestinationColor:        return D3D11_BLEND_DEST_COLOR;
            case Blend::InverseDestinationColor: return D3D11_BLEND_INV_DEST_COLOR;
            case Blend::DestinationAlpha:        return D3D11_BLEND_DEST_ALPHA;
            case Blend::InverseDestinationAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
            case Blend::BlendFactor:             return D3D11_BLEND_BLEND_FACTOR;
            case Blend::InverseBlendFactor:      return D3D11_BLEND_INV_BLEND_FACTOR;
            case Blend::SourceAlphaSaturation:   return D3D11_BLEND_SRC_ALPHA_SAT;
            default:                               return D3D11_BLEND_ONE;
        }
    }

    D3D11_BLEND_OP BlendFunctionToD3D11(int blendFunction)
    {
        switch (static_cast<BlendFunction>(blendFunction))
        {
            case BlendFunction::Add:             return D3D11_BLEND_OP_ADD;
            case BlendFunction::Subtract:        return D3D11_BLEND_OP_SUBTRACT;
            case BlendFunction::ReverseSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
            case BlendFunction::Max:              return D3D11_BLEND_OP_MAX;
            case BlendFunction::Min:              return D3D11_BLEND_OP_MIN;
            default:                                return D3D11_BLEND_OP_ADD;
        }
    }

    D3D11_COMPARISON_FUNC CompareFunctionToD3D11(int compareFunction)
    {
        switch (static_cast<CompareFunction>(compareFunction))
        {
            case CompareFunction::Always:        return D3D11_COMPARISON_ALWAYS;
            case CompareFunction::Never:         return D3D11_COMPARISON_NEVER;
            case CompareFunction::Less:          return D3D11_COMPARISON_LESS;
            case CompareFunction::LessEqual:     return D3D11_COMPARISON_LESS_EQUAL;
            case CompareFunction::Equal:         return D3D11_COMPARISON_EQUAL;
            case CompareFunction::GreaterEqual:  return D3D11_COMPARISON_GREATER_EQUAL;
            case CompareFunction::Greater:       return D3D11_COMPARISON_GREATER;
            case CompareFunction::NotEqual:      return D3D11_COMPARISON_NOT_EQUAL;
            default:                               return D3D11_COMPARISON_ALWAYS;
        }
    }

    D3D11_CULL_MODE CullModeToD3D11(int cullMode)
    {
        // See this function's own header doc: FrontCounterClockwise = FALSE (D3D11's default,
        // matching D3D's native clockwise-is-front convention) is assumed by the caller.
        switch (static_cast<CullMode>(cullMode))
        {
            case CullMode::None:                     return D3D11_CULL_NONE;
            case CullMode::CullClockwiseFace:        return D3D11_CULL_FRONT;
            case CullMode::CullCounterClockwiseFace: return D3D11_CULL_BACK;
            default:                                   return D3D11_CULL_NONE;
        }
    }

    D3D11_FILL_MODE FillModeToD3D11(int fillMode)
    {
        switch (static_cast<FillMode>(fillMode))
        {
            case FillMode::Solid:      return D3D11_FILL_SOLID;
            case FillMode::WireFrame:  return D3D11_FILL_WIREFRAME;
            default:                     return D3D11_FILL_SOLID;
        }
    }

    D3D11_TEXTURE_ADDRESS_MODE TextureAddressModeToD3D11(int addressMode)
    {
        switch (static_cast<TextureAddressMode>(addressMode))
        {
            case TextureAddressMode::Wrap:   return D3D11_TEXTURE_ADDRESS_WRAP;
            case TextureAddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
            case TextureAddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
            default:                           return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    }

    D3D11_FILTER TextureFilterToD3D11(int textureFilter)
    {
        switch (static_cast<TextureFilter>(textureFilter))
        {
            case TextureFilter::Linear:                      return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            case TextureFilter::Point:                       return D3D11_FILTER_MIN_MAG_MIP_POINT;
            case TextureFilter::Anisotropic:                 return D3D11_FILTER_ANISOTROPIC;
            case TextureFilter::LinearMipPoint:               return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            case TextureFilter::PointMipLinear:               return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
            case TextureFilter::MinLinearMagPointMipLinear:  return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
            case TextureFilter::MinLinearMagPointMipPoint:   return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
            case TextureFilter::MinPointMagLinearMipLinear:  return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
            case TextureFilter::MinPointMagLinearMipPoint:   return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
            default:                                           return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        }
    }

    D3D11_STENCIL_OP StencilOperationToD3D11(int stencilOperation)
    {
        switch (static_cast<StencilOperation>(stencilOperation))
        {
            case StencilOperation::Keep:                 return D3D11_STENCIL_OP_KEEP;
            case StencilOperation::Zero:                 return D3D11_STENCIL_OP_ZERO;
            case StencilOperation::Replace:              return D3D11_STENCIL_OP_REPLACE;
            case StencilOperation::Increment:            return D3D11_STENCIL_OP_INCR;
            case StencilOperation::Decrement:            return D3D11_STENCIL_OP_DECR;
            case StencilOperation::IncrementSaturation:  return D3D11_STENCIL_OP_INCR_SAT;
            case StencilOperation::DecrementSaturation:  return D3D11_STENCIL_OP_DECR_SAT;
            case StencilOperation::Invert:               return D3D11_STENCIL_OP_INVERT;
            default:                                       return D3D11_STENCIL_OP_KEEP;
        }
    }
}
