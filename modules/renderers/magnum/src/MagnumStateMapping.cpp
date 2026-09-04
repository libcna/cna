// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumStateMapping.hpp"

#include "System/InvalidOperationException.hpp"

namespace CNA::Internal::Renderers::Magnum
{
    using Renderer = Mg::GL::Renderer;

    Renderer::BlendFunction ToBlendFunction(int xnaBlend)
    {
        switch (xnaBlend)
        {
            case  1: return Renderer::BlendFunction::Zero;
            case  2: return Renderer::BlendFunction::SourceColor;
            case  3: return Renderer::BlendFunction::OneMinusSourceColor;
            case  4: return Renderer::BlendFunction::SourceAlpha;
            case  5: return Renderer::BlendFunction::OneMinusSourceAlpha;
            case  6: return Renderer::BlendFunction::DestinationColor;
            case  7: return Renderer::BlendFunction::OneMinusDestinationColor;
            case  8: return Renderer::BlendFunction::DestinationAlpha;
            case  9: return Renderer::BlendFunction::OneMinusDestinationAlpha;
            case 10: return Renderer::BlendFunction::ConstantColor;
            case 11: return Renderer::BlendFunction::OneMinusConstantColor;
            case 12: return Renderer::BlendFunction::SourceAlphaSaturate;
            default: return Renderer::BlendFunction::One;
        }
    }

    Renderer::BlendEquation ToBlendEquation(int xnaBlendFunction)
    {
        switch (xnaBlendFunction)
        {
            case 1: return Renderer::BlendEquation::Subtract;
            case 2: return Renderer::BlendEquation::ReverseSubtract;
            case 3: return Renderer::BlendEquation::Max;
            case 4: return Renderer::BlendEquation::Min;
            default: return Renderer::BlendEquation::Add;
        }
    }

    Renderer::DepthFunction ToDepthFunction(int xnaCompare)
    {
        switch (xnaCompare)
        {
            case 1: return Renderer::DepthFunction::Never;
            case 2: return Renderer::DepthFunction::Less;
            case 3: return Renderer::DepthFunction::LessOrEqual;
            case 4: return Renderer::DepthFunction::Equal;
            case 5: return Renderer::DepthFunction::GreaterOrEqual;
            case 6: return Renderer::DepthFunction::Greater;
            case 7: return Renderer::DepthFunction::NotEqual;
            default: return Renderer::DepthFunction::Always;
        }
    }

    Renderer::StencilFunction ToStencilFunction(int xnaCompare)
    {
        switch (xnaCompare)
        {
            case 1: return Renderer::StencilFunction::Never;
            case 2: return Renderer::StencilFunction::Less;
            case 3: return Renderer::StencilFunction::LessOrEqual;
            case 4: return Renderer::StencilFunction::Equal;
            case 5: return Renderer::StencilFunction::GreaterOrEqual;
            case 6: return Renderer::StencilFunction::Greater;
            case 7: return Renderer::StencilFunction::NotEqual;
            default: return Renderer::StencilFunction::Always;
        }
    }

    Renderer::StencilOperation ToStencilOperation(int xnaOperation)
    {
        switch (xnaOperation)
        {
            case 1: return Renderer::StencilOperation::Zero;
            case 2: return Renderer::StencilOperation::Replace;
            // XNA's Increment/Decrement wrap, IncrementSaturation/DecrementSaturation clamp --
            // the naming is the reverse of GL's own Incr/IncrWrap, so the pairing below is
            // deliberate rather than a transcription slip.
            case 3: return Renderer::StencilOperation::IncrementWrap;
            case 4: return Renderer::StencilOperation::DecrementWrap;
            case 5: return Renderer::StencilOperation::Increment;
            case 6: return Renderer::StencilOperation::Decrement;
            case 7: return Renderer::StencilOperation::Invert;
            default: return Renderer::StencilOperation::Keep;
        }
    }

    Mg::GL::MeshPrimitive ToMeshPrimitive(PrimitiveType primitive)
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList:  return Mg::GL::MeshPrimitive::Triangles;
            case PrimitiveType::TriangleStrip: return Mg::GL::MeshPrimitive::TriangleStrip;
            case PrimitiveType::LineList:      return Mg::GL::MeshPrimitive::Lines;
            case PrimitiveType::LineStrip:     return Mg::GL::MeshPrimitive::LineStrip;
            case PrimitiveType::PointListEXT:  return Mg::GL::MeshPrimitive::Points;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }
    }

    int VertexCountForPrimitives(PrimitiveType primitive, int primitiveCount)
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }
    }

    void DecomposeTextureFilter(int xnaFilter,
                                Mg::GL::SamplerFilter& minificationOut,
                                Mg::GL::SamplerMipmap& mipmapOut,
                                Mg::GL::SamplerFilter& magnificationOut)
    {
        using Filter = Mg::GL::SamplerFilter;
        using Mipmap = Mg::GL::SamplerMipmap;

        switch (xnaFilter)
        {
            case 1: // Point
                minificationOut = Filter::Nearest;
                mipmapOut       = Mipmap::Nearest;
                magnificationOut = Filter::Nearest;
                break;
            case 3: // LinearMipPoint
                minificationOut = Filter::Linear;
                mipmapOut       = Mipmap::Nearest;
                magnificationOut = Filter::Linear;
                break;
            case 4: // PointMipLinear
                minificationOut = Filter::Nearest;
                mipmapOut       = Mipmap::Linear;
                magnificationOut = Filter::Nearest;
                break;
            case 5: // MinLinearMagPointMipLinear
                minificationOut = Filter::Linear;
                mipmapOut       = Mipmap::Linear;
                magnificationOut = Filter::Nearest;
                break;
            case 6: // MinLinearMagPointMipPoint
                minificationOut = Filter::Linear;
                mipmapOut       = Mipmap::Nearest;
                magnificationOut = Filter::Nearest;
                break;
            case 7: // MinPointMagLinearMipLinear
                minificationOut = Filter::Nearest;
                mipmapOut       = Mipmap::Linear;
                magnificationOut = Filter::Linear;
                break;
            case 8: // MinPointMagLinearMipPoint
                minificationOut = Filter::Nearest;
                mipmapOut       = Mipmap::Nearest;
                magnificationOut = Filter::Linear;
                break;
            case 2: // Anisotropic -- trilinear plus the anisotropy the caller asked for
            default: // Linear
                minificationOut = Filter::Linear;
                mipmapOut       = Mipmap::Linear;
                magnificationOut = Filter::Linear;
                break;
        }
    }

    Mg::GL::SamplerWrapping ToSamplerWrapping(int xnaAddressMode)
    {
        switch (xnaAddressMode)
        {
            case 0:  return Mg::GL::SamplerWrapping::Repeat;
            case 2:  return Mg::GL::SamplerWrapping::MirroredRepeat;
            default: return Mg::GL::SamplerWrapping::ClampToEdge;
        }
    }
}
