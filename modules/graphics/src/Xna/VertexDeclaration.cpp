// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

#include <algorithm>
#include <utility>

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const std::string& VertexDeclaration::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.VertexDeclaration";
        return typeName;
    }

    static int GetTypeSize(VertexElementFormat fmt)
    {
        switch (fmt) {
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

    VertexDeclaration::VertexDeclaration(std::initializer_list<VertexElement> elements)
        : elements_(elements)
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");

        int maxEnd = 0;
        for (const auto& e : elements_) {
            int end = e.getOffsetProperty() + GetTypeSize(e.getVertexElementFormatProperty());
            maxEnd = std::max(maxEnd, end);
        }
        vertexStride_ = maxEnd;
    }

    VertexDeclaration::VertexDeclaration(std::vector<VertexElement> elements)
        : elements_(std::move(elements))
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");

        int maxEnd = 0;
        for (const auto& e : elements_) {
            int end = e.getOffsetProperty() + GetTypeSize(e.getVertexElementFormatProperty());
            maxEnd = std::max(maxEnd, end);
        }
        vertexStride_ = maxEnd;
    }

    VertexDeclaration::VertexDeclaration(
        int vertexStride,
        std::initializer_list<VertexElement> elements)
        : vertexStride_(vertexStride)
        , elements_(elements)
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(
            vertexStride_, "vertexStride");
    }

    VertexDeclaration::VertexDeclaration(
        int vertexStride,
        std::vector<VertexElement> elements)
        : vertexStride_(vertexStride)
        , elements_(std::move(elements))
    {
        if (elements_.empty())
            throw System::ArgumentNullException("elements");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(
            vertexStride_, "vertexStride");
    }
}
