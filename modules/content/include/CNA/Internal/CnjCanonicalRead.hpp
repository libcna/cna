// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal
{
    /**
     * @brief The one canonical reading of the `.cnj` fragments more than one code path consumes
     *        (plans/plan_cnb.md `CNBF-118`).
     *
     * A `.cnj` document is read in two places: by `ContentManager`'s runtime type readers, and by
     * the `.cnj` -> `.cnb` compiler. Two hand-written readings of the same document are two answers
     * to "what does this file mean", and they had drifted -- the compiler parsed JSON while the
     * runtime scanned for substrings, the compiler clamped an out-of-range colour key while the
     * runtime pushed it through `std::stoi`, and neither checked that a number was finite or
     * integral before casting it. **Both routes now call the functions below**, so a document
     * either compiles and loads to the same values or is refused by both.
     *
     * Every reader here is strict on purpose. A `.cnj` is authored, usually by a tool, and a
     * wrong number that is silently truncated, clamped or defaulted produces content that looks
     * plausible and is wrong -- the failure this whole layer exists to prevent. A malformed
     * document is refused, by name, with the field that was wrong.
     */

    /**
     * @brief Largest texture dimension a `.cnj` may declare, in texels.
     *
     * Derived rather than chosen: CNB's texture schema allows at most 16 mip levels
     * (`CnbMaxTextureMipLevels`), and a 16-level chain describes a 65536-texel texture, so this is
     * the largest dimension the compiled form can express. Every dimension product below is
     * computed against it, so `width * height * depth * 4` provably fits a `std::uint64_t`.
     */
    inline constexpr std::int64_t CnjMaxTextureDimension = 65536;

    /**
     * @brief Requires @p value to be a finite JSON number.
     *
     * @param value Member to read; may be null, which is itself a refusal.
     * @param what  Text naming the field and document, placed at the front of any message.
     * @return The value.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p value is absent, is
     *         not a number, or is NaN or infinite. A JSON document can express infinity through
     *         an exponent its grammar allows (`1e400`), so finiteness is a real check rather than
     *         a defensive one.
     */
    [[nodiscard]] double RequireCnjFiniteNumber(const JsonValue* value, const std::string& what);

    /**
     * @brief Requires @p value to be a finite JSON number representable as a `float`.
     *
     * @param value Member to read; may be null.
     * @param what  Text naming the field and document.
     * @return The value narrowed to `float`.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p value is absent, is
     *         not a finite number, or its magnitude is outside what a `float` can represent --
     *         which would otherwise narrow to an infinity nothing downstream expects.
     */
    [[nodiscard]] float RequireCnjSingle(const JsonValue* value, const std::string& what);

    /**
     * @brief Requires @p value to be a JSON number that is finite, exactly integral, and inside
     *        `[minInclusive, maxInclusive]`.
     *
     * Integrality is checked rather than assumed: `static_cast<int>(3.7)` is 3, so a fractional
     * value in a field that means a count or a pixel coordinate would have been silently rounded
     * toward zero and the document would have compiled to something it does not say.
     *
     * @param value        Member to read; may be null.
     * @param what         Text naming the field and document.
     * @param minInclusive Lowest accepted value.
     * @param maxInclusive Highest accepted value.
     * @return The value.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p value is absent, is
     *         not a finite number, is not an exact integer, or is outside the range.
     */
    [[nodiscard]] std::int64_t RequireCnjInteger(const JsonValue* value, const std::string& what,
                                                 std::int64_t minInclusive,
                                                 std::int64_t maxInclusive);

    /**
     * @brief Requires @p object to have a member @p member that is an array of exactly @p count
     *        JSON numbers.
     *
     * The element-type check is the point. Reading `arrayValue[i].numberValue` without it yields
     * `0.0` for a string, a boolean or a nested object, so a malformed array silently became a
     * rectangle at the origin rather than an error.
     *
     * @param object The containing object.
     * @param member The member name.
     * @param count  Exact number of elements required.
     * @param what   Text naming the document, placed at the front of any message.
     * @return The array value.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the member is absent, is
     *         not an array, has a different length, or holds a non-number.
     */
    [[nodiscard]] const JsonValue& RequireCnjNumberArray(const JsonValue& object,
                                                          const char* member, std::size_t count,
                                                          const std::string& what);

    /**
     * @brief Reads a `.cnj` document's optional `"colorKey"` member.
     *
     * Absent means "no colour key" and is not an error. **Present and malformed is an error**: a
     * key with two components, a fractional component or a component outside 0-255 is a document
     * whose author meant something the reader cannot honour, and silently ignoring or clamping it
     * produces art with the wrong pixels transparent.
     *
     * @param root The document's root object.
     * @param what Text naming the document.
     * @return The R, G, B key, or `std::nullopt` when the member is absent.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the member is present
     *         and is not exactly three integers in 0-255.
     */
    [[nodiscard]] std::optional<std::array<std::uint8_t, 3>> ReadCnjColorKey(
        const JsonValue& root, const std::string& what);

    /** @brief A `Texture3D` `.cnj` document's dimensions and the sidecar naming its pixels. */
    struct CnjTexture3DDescription
    {
        /** @brief Volume width in texels; at least 1. */
        std::uint32_t width = 0u;

        /** @brief Volume height in texels; at least 1. */
        std::uint32_t height = 0u;

        /** @brief Volume depth in texels; at least 1. */
        std::uint32_t depth = 0u;

        /**
         * @brief Exact `Rgba8` byte count the sidecar must hold: `width * height * depth * 4`.
         *
         * Computed once here, with checked multiplication, so both routes compare the sidecar
         * against the same number instead of each recomputing it.
         */
        std::uint64_t expectedByteCount = 0u;

        /** @brief The `"data"` member: the sidecar naming the raw pixel bytes. */
        std::string dataFile;
    };

    /**
     * @brief Reads a `Texture3D` `.cnj` document's `width`/`height`/`depth`/`data` members.
     *
     * @param root The document's root object.
     * @param what Text naming the document.
     * @return The description, with @ref CnjTexture3DDescription::expectedByteCount already
     *         computed.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if a dimension is missing,
     *         not a finite integer, outside `1`-`CnjMaxTextureDimension`, if the byte count does
     *         not fit this platform's `std::size_t`, or if `data` is missing or empty.
     */
    [[nodiscard]] CnjTexture3DDescription ReadCnjTexture3DDescription(const JsonValue& root,
                                                                       const std::string& what);

    /** @brief One glyph of a `SpriteFont` `.cnj` document. */
    struct CnjSpriteFontGlyph
    {
        /** @brief The character this glyph draws. */
        SharpRuntime::charcs character = 0;

        /** @brief The glyph's rectangle within the atlas. */
        Microsoft::Xna::Framework::Rectangle source;

        /** @brief The glyph's cropping rectangle. */
        Microsoft::Xna::Framework::Rectangle crop;

        /** @brief Left bearing, advance width and right bearing. */
        Microsoft::Xna::Framework::Vector3 kerning;
    };

    /** @brief A `SpriteFont` `.cnj` document, read once for both the runtime and the compiler. */
    struct CnjSpriteFontDescription
    {
        /** @brief The `"texture"` member: the atlas this font draws from. */
        std::string textureName;

        /** @brief Vertical distance between consecutive lines, in pixels. */
        std::int32_t lineSpacing = 0;

        /** @brief Horizontal spacing added between characters, in pixels. */
        float spacing = 0.0f;

        /** @brief The substitute character, when the document names one. */
        std::optional<SharpRuntime::charcs> defaultCharacter;

        /** @brief The glyphs, in document order. */
        std::vector<CnjSpriteFontGlyph> glyphs;
    };

    /**
     * @brief Reads a `SpriteFont` `.cnj` document.
     *
     * `defaultCharacter` is the **first Unicode code point** of the member's string, not its first
     * byte, and must lie in the Basic Multilingual Plane because `SharpRuntime::charcs` is a
     * UTF-16 code unit. For an ASCII document -- which every authored font descriptor in this
     * repository is -- that is the same character either way. The member may be absent or `null`
     * to say the font has no substitute character; present and any other type is refused.
     *
     * @param root The document's root object.
     * @param what Text naming the document.
     * @return The description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if `texture` is missing or
     *         empty, if `glyphs` is missing, is not an array or is empty, if any glyph lacks a
     *         `char`, a four-element `source`, a four-element `crop` or a three-element `kerning`,
     *         if any number is not finite, is not integral where an integer is required, or is
     *         outside its destination's range, if a character value is not a Unicode scalar in the
     *         Basic Multilingual Plane, or if `defaultCharacter` is present as anything other than
     *         `null` or a non-empty string.
     */
    [[nodiscard]] CnjSpriteFontDescription ReadCnjSpriteFontDescription(const JsonValue& root,
                                                                         const std::string& what);
}
