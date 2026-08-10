#pragma once

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Glide
{
    /**
     * A CPU-readable subset of an XNA VertexDeclaration suitable for Glide's fixed-function
     * vertex path. Glide itself consumes its own post-transform vertex layout; this layout
     * describes only how CNA must decode the application's input bytes first.
     */
    struct GlideVertexLayout
    {
        std::size_t stride = 0;
        int positionOffset = -1;
        int colorOffset = -1;
        int normalOffset = -1;
        int textureCoordinate0Offset = -1;

        [[nodiscard]] bool HasColor() const { return colorOffset >= 0; }
        [[nodiscard]] bool HasNormal() const { return normalOffset >= 0; }
        [[nodiscard]] bool HasTextureCoordinate0() const { return textureCoordinate0Offset >= 0; }
    };

    [[nodiscard]] inline GlideVertexLayout KnownGlideVertexLayout(std::size_t stride)
    {
        GlideVertexLayout layout;
        layout.stride = stride;
        layout.positionOffset = 0;
        if (stride == sizeof(CNA::Internal::Graphics::PositionColorStream))
        {
            layout.colorOffset = 12;
        }
        else if (stride == sizeof(CNA::Internal::Graphics::PositionTextureStream))
        {
            layout.textureCoordinate0Offset = 12;
        }
        else if (stride == sizeof(CNA::Internal::Graphics::PositionColorTextureStream))
        {
            layout.colorOffset = 12;
            layout.textureCoordinate0Offset = 16;
        }
        else if (stride == sizeof(CNA::Internal::Graphics::PositionNormalTextureStream))
        {
            layout.normalOffset = 12;
            layout.textureCoordinate0Offset = 24;
        }
        else
        {
            throw std::runtime_error(
                "GLIDE 3D requires a supported VertexDeclaration for a non-standard vertex stride");
        }
        return layout;
    }

    [[nodiscard]] inline GlideVertexLayout ParseGlideVertexElements(
        int declaredStride,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        if (declaredStride <= 0)
        {
            throw std::runtime_error("GLIDE 3D VertexDeclaration must have a positive vertex stride");
        }

        GlideVertexLayout layout;
        layout.stride = static_cast<std::size_t>(declaredStride);
        const auto addElement = [&](int& destination, const VertexElement& element,
                                    VertexElementFormat requiredFormat, const char* name)
        {
            if (element.getVertexElementFormatProperty() != requiredFormat)
            {
                throw std::runtime_error(std::string("GLIDE 3D requires ") + name +
                    " to use the fixed-function compatible vertex-element format");
            }
            const int offset = element.getOffsetProperty();
            constexpr int kVector2Bytes = static_cast<int>(sizeof(float) * 2u);
            constexpr int kVector3Bytes = static_cast<int>(sizeof(float) * 3u);
            constexpr int kColorBytes = 4;
            const int byteCount = requiredFormat == VertexElementFormat::Vector2 ? kVector2Bytes :
                (requiredFormat == VertexElementFormat::Vector3 ? kVector3Bytes : kColorBytes);
            if (offset < 0 || offset > declaredStride - byteCount)
            {
                throw std::runtime_error(std::string("GLIDE 3D ") + name +
                    " vertex element lies outside its declared vertex stride");
            }
            if (destination >= 0)
            {
                throw std::runtime_error(std::string("GLIDE 3D VertexDeclaration contains duplicate ") + name + " elements");
            }
            destination = offset;
        };

        for (const VertexElement& element : elements)
        {
            const VertexElementUsage usage = element.getVertexElementUsageProperty();
            const int usageIndex = element.getUsageIndexProperty();
            if (usageIndex != 0)
            {
                throw std::runtime_error(
                    "GLIDE 3D supports only usage index 0 for its fixed-function vertex semantics");
            }
            switch (usage)
            {
                case VertexElementUsage::Position:
                    addElement(layout.positionOffset, element, VertexElementFormat::Vector3, "Position");
                    break;
                case VertexElementUsage::Color:
                    addElement(layout.colorOffset, element, VertexElementFormat::Color, "Color");
                    break;
                case VertexElementUsage::Normal:
                    addElement(layout.normalOffset, element, VertexElementFormat::Vector3, "Normal");
                    break;
                case VertexElementUsage::TextureCoordinate:
                    addElement(layout.textureCoordinate0Offset, element, VertexElementFormat::Vector2,
                               "TextureCoordinate0");
                    break;
                default:
                    throw std::runtime_error(
                        "GLIDE 3D VertexDeclaration contains a semantic outside the fixed-function subset");
            }
        }
        if (layout.positionOffset < 0)
        {
            throw std::runtime_error("GLIDE 3D VertexDeclaration requires a Vector3 Position0 element");
        }
        return layout;
    }

    [[nodiscard]] inline GlideVertexLayout ParseGlideVertexDeclaration(
        const Microsoft::Xna::Framework::Graphics::VertexDeclaration& declaration)
    {
        return ParseGlideVertexElements(declaration.getVertexStrideProperty(),
                                        declaration.GetVertexElements());
    }
} // namespace CNA::Internal::Renderers::Glide
