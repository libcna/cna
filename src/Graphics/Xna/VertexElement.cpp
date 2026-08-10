// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    std::string VertexElement::ToString() const
    {
        auto fmtName = [](VertexElementFormat f) -> std::string {
            switch (f) {
                case VertexElementFormat::Single:           return "Single";
                case VertexElementFormat::Vector2:          return "Vector2";
                case VertexElementFormat::Vector3:          return "Vector3";
                case VertexElementFormat::Vector4:          return "Vector4";
                case VertexElementFormat::Color:            return "Color";
                case VertexElementFormat::Byte4:            return "Byte4";
                case VertexElementFormat::Short2:           return "Short2";
                case VertexElementFormat::Short4:           return "Short4";
                case VertexElementFormat::NormalizedShort2: return "NormalizedShort2";
                case VertexElementFormat::NormalizedShort4: return "NormalizedShort4";
                case VertexElementFormat::HalfVector2:      return "HalfVector2";
                case VertexElementFormat::HalfVector4:      return "HalfVector4";
                default:                                    return std::to_string(static_cast<int>(f));
            }
        };
        auto usageName = [](VertexElementUsage u) -> std::string {
            switch (u) {
                case VertexElementUsage::Position:         return "Position";
                case VertexElementUsage::Color:            return "Color";
                case VertexElementUsage::TextureCoordinate:return "TextureCoordinate";
                case VertexElementUsage::Normal:           return "Normal";
                case VertexElementUsage::Binormal:         return "Binormal";
                case VertexElementUsage::Tangent:          return "Tangent";
                case VertexElementUsage::BlendIndices:     return "BlendIndices";
                case VertexElementUsage::BlendWeight:      return "BlendWeight";
                case VertexElementUsage::Depth:            return "Depth";
                case VertexElementUsage::Fog:              return "Fog";
                case VertexElementUsage::PointSize:        return "PointSize";
                case VertexElementUsage::Sample:           return "Sample";
                case VertexElementUsage::TessellateFactor: return "TessellateFactor";
                default:                                   return std::to_string(static_cast<int>(u));
            }
        };
        return "{{Offset:" + std::to_string(offset_)
             + " Format:" + fmtName(vertexElementFormat_)
             + " Usage:" + usageName(vertexElementUsage_)
             + " UsageIndex: " + std::to_string(usageIndex_)
             + "}}";
    }
}
