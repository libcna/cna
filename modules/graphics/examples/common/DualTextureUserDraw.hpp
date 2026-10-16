// SPDX-License-Identifier: MS-PL
#pragma once

// DualTextureEffect reads TEXCOORD0 and TEXCOORD1, and XNA's GraphicsDevice refuses a draw whose
// declaration lacks one ("TextureCoordinate1 is missing"), DrawUserPrimitives included. XNA has no
// built-in vertex type with two coordinate sets, so these draw single-UV VertexPositionTexture and
// VertexPositionColorTexture geometry -- or build vertex buffers of it -- through a declaration whose
// second set repeats the first, which is what the example programs that use them always fed the
// effect.

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Examples
{
    /** @brief Position, TextureCoordinate0 and an equal TextureCoordinate1: 28 bytes. */
    struct DualTextureUserVertex
    {
        float x, y, z;
        float u0, v0;
        float u1, v1;
    };

    /**
     * @brief The declaration of DualTextureUserVertex.
     *
     * @return Position at 0, TextureCoordinate0 at 12, TextureCoordinate1 at 20, stride 28.
     */
    inline Microsoft::Xna::Framework::Graphics::VertexDeclaration DualTextureUserDeclaration()
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        return VertexDeclaration(28, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });
    }

    /**
     * @brief Copies @p count vertices from @p offset, each with its coordinate set repeated.
     *
     * @param vertices Source geometry.
     * @param offset First vertex copied.
     * @param count Number of vertices copied.
     * @return The dual-coordinate records.
     */
    inline std::vector<DualTextureUserVertex> ToDualTextureUserVertices(
        const Microsoft::Xna::Framework::Graphics::VertexPositionTexture* vertices, int offset,
        int count)
    {
        std::vector<DualTextureUserVertex> out(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const auto& v = vertices[offset + i];
            const auto uv = v.TextureCoordinate;
            out[static_cast<std::size_t>(i)] = {v.Position.X, v.Position.Y, v.Position.Z,
                                                uv.X, uv.Y, uv.X, uv.Y};
        }
        return out;
    }

    /**
     * @brief DrawUserPrimitives for a DualTextureEffect over VertexPositionTexture geometry.
     *
     * @param device The device to draw with.
     * @param type The primitive type.
     * @param vertices Source geometry.
     * @param vertexOffset First vertex drawn.
     * @param primitiveCount Number of primitives.
     */
    inline void DrawUserDualTexturePrimitives(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        Microsoft::Xna::Framework::Graphics::PrimitiveType type,
        const Microsoft::Xna::Framework::Graphics::VertexPositionTexture* vertices,
        int vertexOffset, int primitiveCount)
    {
        using Microsoft::Xna::Framework::Graphics::PrimitiveType;
        int vertexCount = primitiveCount * 3;
        if (type == PrimitiveType::TriangleStrip) vertexCount = primitiveCount + 2;
        else if (type == PrimitiveType::LineList) vertexCount = primitiveCount * 2;
        else if (type == PrimitiveType::LineStrip) vertexCount = primitiveCount + 1;
        const auto records = ToDualTextureUserVertices(vertices, vertexOffset, vertexCount);
        device.DrawUserPrimitives(type, records.data(), 0, primitiveCount,
                                  DualTextureUserDeclaration());
    }


    /** @brief Position, Color, TextureCoordinate0 and an equal TextureCoordinate1: 32 bytes. */
    struct DualTextureColorUserVertex
    {
        float x, y, z;
        std::uint32_t packedColor;
        float u0, v0;
        float u1, v1;
    };

    /**
     * @brief The declaration of DualTextureColorUserVertex.
     *
     * @return Position at 0, Color0 at 12, TextureCoordinate0 at 16, TextureCoordinate1 at 24,
     *         stride 32.
     */
    inline Microsoft::Xna::Framework::Graphics::VertexDeclaration DualTextureColorUserDeclaration()
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        return VertexDeclaration(32, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });
    }

    /**
     * @brief Copies @p count coloured vertices from @p offset, each with its coordinate set repeated.
     *
     * @param vertices Source geometry.
     * @param offset First vertex copied.
     * @param count Number of vertices copied.
     * @return The dual-coordinate records.
     */
    inline std::vector<DualTextureColorUserVertex> ToDualTextureColorUserVertices(
        const Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture* vertices, int offset,
        int count)
    {
        std::vector<DualTextureColorUserVertex> out(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const auto& v = vertices[offset + i];
            const auto uv = v.TextureCoordinate;
            out[static_cast<std::size_t>(i)] = {v.Position.X, v.Position.Y, v.Position.Z,
                                                v.Color.getPackedValueProperty(),
                                                uv.X, uv.Y, uv.X, uv.Y};
        }
        return out;
    }

    /**
     * @brief DrawUserPrimitives for a vertex-coloured DualTextureEffect over
     * VertexPositionColorTexture geometry.
     *
     * @param device The device to draw with.
     * @param type The primitive type.
     * @param vertices Source geometry.
     * @param vertexOffset First vertex drawn.
     * @param primitiveCount Number of primitives.
     */
    inline void DrawUserDualTexturePrimitives(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        Microsoft::Xna::Framework::Graphics::PrimitiveType type,
        const Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture* vertices,
        int vertexOffset, int primitiveCount)
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        int vertexCount = primitiveCount * 3;
        if (type == PrimitiveType::TriangleStrip) vertexCount = primitiveCount + 2;
        else if (type == PrimitiveType::LineList) vertexCount = primitiveCount * 2;
        else if (type == PrimitiveType::LineStrip) vertexCount = primitiveCount + 1;
        const auto records = ToDualTextureColorUserVertices(vertices, vertexOffset, vertexCount);
        device.DrawUserPrimitives(type, records.data(), 0, primitiveCount,
                                  DualTextureColorUserDeclaration());
    }

    /**
     * @brief DrawUserIndexedPrimitives (16-bit indices) for a DualTextureEffect over
     * VertexPositionTexture geometry.
     *
     * @param device The device to draw with.
     * @param type The primitive type.
     * @param vertices Source geometry.
     * @param vertexOffset First vertex the indices address.
     * @param numVertices Number of vertices the indices address.
     * @param indices Index data.
     * @param indexOffset First index drawn.
     * @param primitiveCount Number of primitives.
     */
    inline void DrawUserIndexedDualTexturePrimitives(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        Microsoft::Xna::Framework::Graphics::PrimitiveType type,
        const Microsoft::Xna::Framework::Graphics::VertexPositionTexture* vertices,
        int vertexOffset, int numVertices, const std::uint16_t* indices, int indexOffset,
        int primitiveCount)
    {
        const auto records = ToDualTextureUserVertices(vertices, vertexOffset, numVertices);
        device.DrawUserIndexedPrimitives(type, records.data(), 0, numVertices, indices, indexOffset,
                                         primitiveCount, DualTextureUserDeclaration());
    }
}
