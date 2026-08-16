// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"

#include <array>
#include <span>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Fna3d
{
    /**
     * @brief The canonical stride-keyed vertex layout, as FNA3D vertex elements.
     *
     * FNA3D binds a real `FNA3D_VertexDeclaration` per stream, so whenever the caller supplies a
     * `VertexDeclaration` this renderer uses it verbatim and never consults this table. It exists
     * only for the internal routes that bind no public `VertexBufferBinding` at all -- SpriteBatch,
     * `DrawUser*`, `DrawColoredPrimitives`, and REMED-GFX-233's legacy empty-declaration buffer --
     * where the byte stride of the staged buffer is the only description available.
     *
     * The offsets and semantics are the repository-wide layout every other CNA renderer dispatches
     * on (`D3DCommon::InputElementsForStride`, `EasyGLRenderer::ApplyLayout`), reproduced here in
     * XNA vocabulary because that is what FNA3D takes.
     *
     * @param strideInBytes Byte stride of one vertex.
     * @return A non-owning view of immutable elements describing that stride. The storage is
     *         static so the ordinary draw path never allocates merely to name a known layout.
     * @throws std::runtime_error for a stride no CNA layout defines, rather than binding a
     *         plausible-looking layout that would silently reinterpret the caller's bytes.
     */
    [[nodiscard]] inline std::span<const FNA3D_VertexElement> ElementsForStride(int strideInBytes)
    {
        const auto element = [](int offset, FNA3D_VertexElementFormat format,
                                FNA3D_VertexElementUsage usage, int usageIndex = 0) {
            FNA3D_VertexElement result{};
            result.offset = offset;
            result.vertexElementFormat = format;
            result.vertexElementUsage = usage;
            result.usageIndex = usageIndex;
            return result;
        };

        static const std::array positionColor{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_COLOR,
                    FNA3D_VERTEXELEMENTUSAGE_COLOR),
        };
        static const std::array positionTexture{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
        };
        static const std::array positionColorTexture{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_COLOR,
                    FNA3D_VERTEXELEMENTUSAGE_COLOR),
            element(16, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
        };
        static const std::array positionNormalTexture{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_NORMAL),
            element(24, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
        };
        static const std::array positionNormalTangentTexture{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_NORMAL),
            element(24, FNA3D_VERTEXELEMENTFORMAT_VECTOR4,
                    FNA3D_VERTEXELEMENTUSAGE_TANGENT),
            element(40, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
        };
        static const std::array positionNormalTextureSkinned{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_NORMAL),
            element(24, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
            element(32, FNA3D_VERTEXELEMENTFORMAT_VECTOR4,
                    FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT),
            element(48, FNA3D_VERTEXELEMENTFORMAT_BYTE4,
                    FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES),
        };
        static const std::array positionNormalTextureSkinnedColor{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_NORMAL),
            element(24, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
            element(32, FNA3D_VERTEXELEMENTFORMAT_VECTOR4,
                    FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT),
            element(48, FNA3D_VERTEXELEMENTFORMAT_BYTE4,
                    FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES),
            element(52, FNA3D_VERTEXELEMENTFORMAT_COLOR,
                    FNA3D_VERTEXELEMENTUSAGE_COLOR),
        };
        static const std::array positionNormalTangentTextureSkinned{
            element(0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_POSITION),
            element(12, FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
                    FNA3D_VERTEXELEMENTUSAGE_NORMAL),
            element(24, FNA3D_VERTEXELEMENTFORMAT_VECTOR4,
                    FNA3D_VERTEXELEMENTUSAGE_TANGENT),
            element(40, FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
                    FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE),
            element(48, FNA3D_VERTEXELEMENTFORMAT_VECTOR4,
                    FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT),
            element(64, FNA3D_VERTEXELEMENTFORMAT_BYTE4,
                    FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES),
        };

        switch (strideInBytes)
        {
            // VertexPositionColor
            case 16:
                return positionColor;

            // VertexPositionTexture
            case 20:
                return positionTexture;

            // VertexPositionColorTexture -- also the SpriteBatch vertex.
            case 24:
                return positionColorTexture;

            // VertexPositionNormalTexture
            case 32:
                return positionNormalTexture;

            // VertexPositionNormalTangentTexture
            case 48:
                return positionNormalTangentTexture;

            // VertexPositionNormalTextureSkinned
            case 52:
                return positionNormalTextureSkinned;

            // VertexPositionNormalTextureSkinned + per-vertex Color appended
            case 56:
                return positionNormalTextureSkinnedColor;

            // VertexPositionNormalTangentTextureSkinned
            case 68:
                return positionNormalTangentTextureSkinned;

            default:
                throw std::runtime_error(
                    "FNA3D renderer: no CNA vertex layout is defined for stride " +
                    std::to_string(strideInBytes) +
                    " and this draw supplied no VertexDeclaration. Bind the buffer through a "
                    "VertexBuffer with its declaration -- FNA3D binds real per-stream "
                    "declarations and never guesses a layout.");
        }
    }
}
