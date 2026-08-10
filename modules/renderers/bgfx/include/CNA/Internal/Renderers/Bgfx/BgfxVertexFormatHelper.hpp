// SPDX-License-Identifier: MS-PL
#pragma once

#include <bgfx/bgfx.h>
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace CNA::Internal::Renderers::Bgfx
{
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    /**
     * @brief Describes how a VertexElementFormat maps onto a bgfx VertexLayout attribute.
     *
     * Pass these fields directly to bgfx::VertexLayout::add().
     */
    struct BgfxAttribInfo
    {
        bgfx::AttribType::Enum type;  ///< Scalar component type.
        uint8_t                num;   ///< Number of components (1–4).
        bool                   normalized; ///< True if integer components are normalized to [0,1]/[-1,1].
        bool                   asInt;      ///< True if integer data should remain as integers in the shader.
    };

    /**
     * @brief Maps an XNA VertexElementFormat to bgfx VertexLayout attribute parameters.
     *
     * These mappings mirror the XNA/FNA GetTypeSize() byte widths and select the
     * closest bgfx::AttribType for each packed data representation.
     *
     * Unsupported formats (future extensions) fall back to Float/1/false/false.
     *
     * @param fmt The XNA vertex element format.
     * @return BgfxAttribInfo suitable for bgfx::VertexLayout::add().
     */
    inline BgfxAttribInfo VertexElementFormatToBgfx(VertexElementFormat fmt)
    {
        switch (fmt)
        {
            case VertexElementFormat::Single:           return { bgfx::AttribType::Float, 1, false, false };
            case VertexElementFormat::Vector2:          return { bgfx::AttribType::Float, 2, false, false };
            case VertexElementFormat::Vector3:          return { bgfx::AttribType::Float, 3, false, false };
            case VertexElementFormat::Vector4:          return { bgfx::AttribType::Float, 4, false, false };
            case VertexElementFormat::Color:            return { bgfx::AttribType::Uint8, 4, true,  false };
            case VertexElementFormat::Byte4:            return { bgfx::AttribType::Uint8, 4, false, true  };
            case VertexElementFormat::Short2:           return { bgfx::AttribType::Int16, 2, false, true  };
            case VertexElementFormat::Short4:           return { bgfx::AttribType::Int16, 4, false, true  };
            case VertexElementFormat::NormalizedShort2: return { bgfx::AttribType::Int16, 2, true,  false };
            case VertexElementFormat::NormalizedShort4: return { bgfx::AttribType::Int16, 4, true,  false };
            case VertexElementFormat::HalfVector2:      return { bgfx::AttribType::Half,  2, false, false };
            case VertexElementFormat::HalfVector4:      return { bgfx::AttribType::Half,  4, false, false };
            default:                                    return { bgfx::AttribType::Float, 1, false, false };
        }
    }

    /**
     * @brief Maps an XNA VertexElementUsage + usageIndex to a bgfx::Attrib slot.
     *
     * Bgfx supports Position, Normal, Tangent, Bitangent, Color0–Color3,
     * TexCoord0–TexCoord7, Indices, and Weight.  XNA usages with no bgfx
     * equivalent (Depth, Fog, PointSize, Sample, TessellateFactor) return
     * bgfx::Attrib::Count as a sentinel indicating "unsupported".
     *
     * @param usage      XNA semantic usage.
     * @param usageIndex Channel index for multi-channel usages (Color, TexCoord).
     * @return bgfx::Attrib::Enum slot, or bgfx::Attrib::Count if unsupported.
     */
    inline bgfx::Attrib::Enum VertexElementUsageToBgfxAttrib(VertexElementUsage usage,
                                                               int usageIndex = 0)
    {
        switch (usage)
        {
            case VertexElementUsage::Position:    return bgfx::Attrib::Position;
            case VertexElementUsage::Normal:      return bgfx::Attrib::Normal;
            case VertexElementUsage::Tangent:     return bgfx::Attrib::Tangent;
            case VertexElementUsage::Binormal:    return bgfx::Attrib::Bitangent;
            case VertexElementUsage::BlendIndices:return bgfx::Attrib::Indices;
            case VertexElementUsage::BlendWeight: return bgfx::Attrib::Weight;
            case VertexElementUsage::Color:
                switch (usageIndex)
                {
                    case 0:  return bgfx::Attrib::Color0;
                    case 1:  return bgfx::Attrib::Color1;
                    case 2:  return bgfx::Attrib::Color2;
                    case 3:  return bgfx::Attrib::Color3;
                    default: return bgfx::Attrib::Color0;
                }
            case VertexElementUsage::TextureCoordinate:
                switch (usageIndex)
                {
                    case 0:  return bgfx::Attrib::TexCoord0;
                    case 1:  return bgfx::Attrib::TexCoord1;
                    case 2:  return bgfx::Attrib::TexCoord2;
                    case 3:  return bgfx::Attrib::TexCoord3;
                    case 4:  return bgfx::Attrib::TexCoord4;
                    case 5:  return bgfx::Attrib::TexCoord5;
                    case 6:  return bgfx::Attrib::TexCoord6;
                    case 7:  return bgfx::Attrib::TexCoord7;
                    default: return bgfx::Attrib::TexCoord0;
                }
            // Depth, Fog, PointSize, Sample, TessellateFactor: no bgfx equivalent
            default: return bgfx::Attrib::Count;
        }
    }

    /**
     * @brief Returns the byte size of one vertex element of the given format.
     *
     * Matches the FNA GetTypeSize() values used by VertexDeclaration stride
     * auto-computation.  Identical to VulkanVertexFormatHelper::VertexElementFormatSize().
     *
     * @param fmt The XNA vertex element format.
     * @return Byte count (4–16), or 0 for unknown formats.
     */
    inline int VertexElementFormatSize(VertexElementFormat fmt)
    {
        switch (fmt)
        {
            case VertexElementFormat::Single:           return 4;
            case VertexElementFormat::Vector2:          return 8;
            case VertexElementFormat::Vector3:          return 12;
            case VertexElementFormat::Vector4:          return 16;
            case VertexElementFormat::Color:            return 4;
            case VertexElementFormat::Byte4:            return 4;
            case VertexElementFormat::Short2:           return 4;
            case VertexElementFormat::Short4:           return 8;
            case VertexElementFormat::NormalizedShort2: return 4;
            case VertexElementFormat::NormalizedShort4: return 8;
            case VertexElementFormat::HalfVector2:      return 4;
            case VertexElementFormat::HalfVector4:      return 8;
            default:                                    return 0;
        }
    }

} // namespace CNA::Internal::Renderers::Bgfx
