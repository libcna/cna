// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Content::Cnb
{
    /** @brief Chunk identifiers defined by the `SpriteFont` asset schema (plans/plan_cnb.md `CNBF-102`). */
    namespace CnbSpriteFontChunk
    {
        /** @brief `FONT` -- glyph count, spacing and the default character. Mandatory, exactly one. */
        inline constexpr CnbChunkId Header = MakeChunkId('F', 'O', 'N', 'T');

        /** @brief `GLYP` -- each glyph's source rectangle within the atlas. Mandatory, exactly one. */
        inline constexpr CnbChunkId GlyphBounds = MakeChunkId('G', 'L', 'Y', 'P');

        /** @brief `CROP` -- each glyph's offset/cropping rectangle. Mandatory, exactly one. */
        inline constexpr CnbChunkId Cropping = MakeChunkId('C', 'R', 'O', 'P');

        /** @brief `KERN` -- each glyph's left bearing, width and right bearing. Mandatory, exactly one. */
        inline constexpr CnbChunkId Kerning = MakeChunkId('K', 'E', 'R', 'N');

        /** @brief `CHAR` -- the sorted character map. Mandatory, exactly one. */
        inline constexpr CnbChunkId Characters = MakeChunkId('C', 'H', 'A', 'R');
    }

    /** @brief Highest `SpriteFont` schema version this build understands. */
    inline constexpr std::uint32_t CnbSpriteFontSchemaVersion = 1u;

    /** @brief Bytes the `FONT` chunk occupies. */
    inline constexpr std::uint32_t CnbSpriteFontHeaderStride = 24u;

    /** @brief Bytes one `GLYP` or `CROP` rectangle occupies. */
    inline constexpr std::uint32_t CnbSpriteFontRectangleStride = 16u;

    /** @brief Bytes one `KERN` entry occupies. */
    inline constexpr std::uint32_t CnbSpriteFontKerningStride = 12u;

    /** @brief Bytes one `CHAR` entry occupies. */
    inline constexpr std::uint32_t CnbSpriteFontCharacterStride = 4u;

    /**
     * @brief Ceiling on the number of glyphs a file may declare.
     *
     * Above every real font by a wide margin, and low enough that a hostile count is refused
     * before the reader multiplies it by four stride sizes.
     */
    inline constexpr std::uint32_t CnbMaxSpriteFontGlyphs = 65536u;

    /**
     * @brief The decoded contents of a `SpriteFont` `.cnb`, independent of any GPU object.
     *
     * The four per-glyph arrays are parallel and all exactly `characters.size()` long, which is
     * the invariant `SpriteFont`'s own constructor requires and this schema enforces on both
     * sides rather than trusting.
     */
    struct CnbSpriteFontData
    {
        /** @brief The glyph atlas, embedded in the same file. */
        CnbTextureData atlas;

        /** @brief Each glyph's source rectangle within the atlas. */
        std::vector<Microsoft::Xna::Framework::Rectangle> glyphBounds;

        /** @brief Each glyph's offset/cropping rectangle. */
        std::vector<Microsoft::Xna::Framework::Rectangle> cropping;

        /** @brief Each glyph's left bearing, width and right bearing. */
        std::vector<Microsoft::Xna::Framework::Vector3> kerning;

        /** @brief The characters this font renders, in ascending order. */
        std::vector<SharpRuntime::charcs> characters;

        /** @brief Vertical distance between text lines, in pixels. */
        std::int32_t lineSpacing = 0;

        /** @brief Extra horizontal spacing applied between characters. */
        float spacing = 0.0f;

        /** @brief Fallback glyph, or `std::nullopt` to throw on a missing character. */
        std::optional<SharpRuntime::charcs> defaultCharacter;
    };

    /**
     * @brief Encodes a `SpriteFont` as a complete `.cnb` byte image, atlas included.
     *
     * @param data        The font to encode.
     * @param contentName Logical content name recorded in the `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the parallel arrays are
     *         not all the same length, the character map is not strictly ascending, the default
     *         character is not one of the characters, or the atlas is inconsistent.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeSpriteFontToCnb(
        const CnbSpriteFontData& data, const std::string& contentName = {});

    /**
     * @brief Decodes a `SpriteFont` from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded font description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not a
     *         `SpriteFont`, uses an unsupported schema version, is missing a mandatory chunk, or
     *         declares counts that disagree with its chunk lengths.
     */
    [[nodiscard]] CnbSpriteFontData DecodeSpriteFontFromCnb(const CnbDocument& document);
}
