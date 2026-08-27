// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Chunk identifiers shared by the `Texture2D`, `TextureCube` and `Texture3D` schemas
     *        (plans/plan_cnb.md `CNBF-101A`, `CNBF-101B`, `CNBF-101C`).
     *
     * The three asset types differ only in how the header's `faceCount` and `depth` are
     * constrained, so they share one chunk layout rather than repeating it three times. The asset
     * type identifier in the container header is what distinguishes them, exactly as it should be.
     */
    namespace CnbTextureChunk
    {
        /** @brief `TEXH` -- dimensions, face and mip counts, representation count. Mandatory, exactly one. */
        inline constexpr CnbChunkId Header = MakeChunkId('T', 'E', 'X', 'H');

        /** @brief `TEXR` -- the representation descriptor table. Mandatory, exactly one. */
        inline constexpr CnbChunkId Representations = MakeChunkId('T', 'E', 'X', 'R');

        /** @brief `TEXD` -- one mip level's payload bytes. Mandatory, one per level per representation. */
        inline constexpr CnbChunkId Payload = MakeChunkId('T', 'E', 'X', 'D');
    }

    /** @brief Highest texture schema version this build understands, for all three asset types. */
    inline constexpr std::uint32_t CnbTextureSchemaVersion = 1u;

    /** @brief Bytes the `TEXH` chunk occupies. */
    inline constexpr std::uint32_t CnbTextureHeaderStride = 24u;

    /** @brief Bytes one `TEXR` descriptor occupies. */
    inline constexpr std::uint32_t CnbTextureRepresentationStride = 24u;

    /** @brief Number of faces a `TextureCube` has, in the fixed order +X, -X, +Y, -Y, +Z, -Z. */
    inline constexpr std::uint32_t CnbTextureCubeFaceCount = 6u;

    /**
     * @brief Ceiling on the number of mip levels a file may declare.
     *
     * A mip chain halves each dimension, so 16 levels already describes a 65536-texel texture --
     * far past any renderer's limit. The ceiling exists so a hostile count is refused on sight
     * rather than after the reader has tried to account for four billion levels.
     */
    inline constexpr std::uint32_t CnbMaxTextureMipLevels = 16u;

    /** @brief Ceiling on the number of representations a file may declare. */
    inline constexpr std::uint32_t CnbMaxTextureRepresentations = 8u;

    /**
     * @brief One encoding of the whole texture: a format, plus every level's bytes in that format.
     *
     * A file may carry the same image several times over — once as `Rgba8`, once as `Bc7`, once as
     * something else — so a runtime can pick whichever its GPU actually supports without a second
     * asset. Levels are ordered **face-major, then mip**: for a cube map that is `+X` mip 0, `+X`
     * mip 1, …, then `-X` mip 0, and so on.
     */
    struct CnbTextureRepresentation
    {
        /** @brief The storage format of every level in this representation. */
        CnbTextureFormat format = CnbTextureFormat::Unknown;

        /** @brief `faceCount * mipCount` payloads, face-major then mip. */
        std::vector<std::vector<std::uint8_t>> levels;
    };

    /**
     * @brief The decoded contents of a texture `.cnb`, independent of any GPU object.
     *
     * This is the neutral representation the codec produces and consumes; turning it into a real
     * `Texture2D`/`TextureCube`/`Texture3D` needs a `GraphicsDevice` and therefore lives with the
     * `ContentManager`, exactly as the `Model` schema splits.
     */
    struct CnbTextureData
    {
        /** @brief Width of mip level 0, in texels. */
        std::uint32_t width = 0u;

        /** @brief Height of mip level 0, in texels. */
        std::uint32_t height = 0u;

        /** @brief Depth of mip level 0, in texels. 1 for a 2D or cube texture. */
        std::uint32_t depth = 1u;

        /** @brief Number of faces. 1 for `Texture2D`/`Texture3D`, 6 for `TextureCube`. */
        std::uint32_t faceCount = 1u;

        /** @brief Number of mip levels, at least 1. */
        std::uint32_t mipCount = 1u;

        /** @brief The available encodings, in the order the writer recorded them. */
        std::vector<CnbTextureRepresentation> representations;
    };

    /**
     * @brief The dimensions of one mip level of a texture whose level 0 is @p data.
     *
     * Each dimension halves per level and never falls below 1, which is the standard mip rule and
     * the one the level byte sizes are computed against.
     *
     * @param data  The texture whose level-0 dimensions are the starting point.
     * @param level The mip level to measure, `0` being the full size.
     * @param width  Receives the level's width.
     * @param height Receives the level's height.
     * @param depth  Receives the level's depth.
     */
    void CnbTextureLevelDimensions(const CnbTextureData& data, std::uint32_t level,
                                   std::uint32_t& width, std::uint32_t& height,
                                   std::uint32_t& depth);

    /**
     * @brief Builds a single-representation, single-mip `Rgba8` 2D texture description.
     *
     * A convenience for the common case of a decoded PNG, which is what CNB schema 1 encodes.
     *
     * @param width  Texture width in texels; must be at least 1.
     * @param height Texture height in texels; must be at least 1.
     * @param rgba   Exactly `width * height * 4` bytes, in R, G, B, A order.
     * @return The texture description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if a dimension is 0 or
     *         @p rgba is not exactly the required length.
     */
    [[nodiscard]] CnbTextureData MakeRgba8Texture2DData(std::uint32_t width, std::uint32_t height,
                                                        std::vector<std::uint8_t> rgba);

    /**
     * @brief Encodes a `Texture2D` as a complete `.cnb` byte image.
     *
     * @param data        The texture to encode. `faceCount` must be 1 and `depth` must be 1.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p data is inconsistent,
     *         declares a level whose byte count disagrees with its dimensions, or uses a format
     *         CNB schema 1 does not encode.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeTexture2DToCnb(const CnbTextureData& data,
                                                                 const std::string& contentName = {});

    /**
     * @brief Encodes a `TextureCube` as a complete `.cnb` byte image.
     *
     * @param data        The texture to encode. `faceCount` must be 6, `depth` 1, and width must
     *                    equal height, because a cube face is square.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on the same conditions as
     *         EncodeTexture2DToCnb(), plus a non-square or non-six-faced description.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeTextureCubeToCnb(const CnbTextureData& data,
                                                                   const std::string& contentName = {});

    /**
     * @brief Encodes a `Texture3D` as a complete `.cnb` byte image.
     *
     * @param data        The texture to encode. `faceCount` must be 1; `depth` may be any positive
     *                    value and halves per mip level like the other two dimensions.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on the same conditions as
     *         EncodeTexture2DToCnb().
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeTexture3DToCnb(const CnbTextureData& data,
                                                                 const std::string& contentName = {});

    /**
     * @brief Decodes a `Texture2D` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded texture description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not a
     *         `Texture2D`, uses an unsupported schema version, is missing a mandatory chunk, or
     *         declares counts, dimensions or payload lengths that disagree with each other.
     */
    [[nodiscard]] CnbTextureData DecodeTexture2DFromCnb(const CnbDocument& document);

    /**
     * @brief Decodes a `TextureCube` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded texture description, with `faceCount` 6.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on the same conditions as
     *         DecodeTexture2DFromCnb().
     */
    [[nodiscard]] CnbTextureData DecodeTextureCubeFromCnb(const CnbDocument& document);

    /**
     * @brief Decodes a `Texture3D` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded texture description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on the same conditions as
     *         DecodeTexture2DFromCnb().
     */
    [[nodiscard]] CnbTextureData DecodeTexture3DFromCnb(const CnbDocument& document);

    /**
     * @brief Picks the representation a caller should upload, preferring the earliest supported one.
     *
     * The writer records representations in preference order, so a runtime that walks the list and
     * takes the first format it can upload gets the author's intended choice. CNB schema 1 writes
     * exactly one representation, but the selection is implemented from the start so a file with
     * several is not a future format change.
     *
     * @param data      The decoded texture.
     * @param supported Predicate returning true for a format the caller can upload.
     * @return Index into `data.representations`, or `data.representations.size()` when none of the
     *         available formats is supported.
     */
    [[nodiscard]] std::size_t SelectCnbTextureRepresentation(
        const CnbTextureData& data, const std::function<bool(CnbTextureFormat)>& supported);
}
