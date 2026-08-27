// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/CnjCanonicalRead.hpp"

#include <cmath>
#include <limits>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;

namespace CNA::Internal
{
    namespace
    {
        [[noreturn]] void Fail(const std::string& what, const std::string& detail)
        {
            throw ContentLoadException(what + ": " + detail);
        }

        /// Renders a double for a diagnostic without the trailing zeroes std::to_string adds, so
        /// "3.7" reads as 3.7 rather than 3.700000.
        std::string Describe(double value)
        {
            if (std::isnan(value)) { return "NaN"; }
            if (std::isinf(value)) { return value < 0.0 ? "-infinity" : "infinity"; }
            std::string text = std::to_string(value);
            while (text.size() > 1u && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
            return text;
        }

        /// The first Unicode code point of a UTF-8 string, or nullopt when the string is empty or
        /// not well-formed UTF-8 at its first character.
        std::optional<std::uint32_t> FirstCodePoint(const std::string& text)
        {
            if (text.empty()) { return std::nullopt; }
            const auto lead = static_cast<std::uint8_t>(text[0]);
            std::size_t extra = 0;
            std::uint32_t codePoint = 0;
            std::uint32_t lowestLegal = 0;
            if (lead < 0x80u) { return static_cast<std::uint32_t>(lead); }
            if ((lead & 0xE0u) == 0xC0u) { extra = 1; codePoint = lead & 0x1Fu; lowestLegal = 0x80u; }
            else if ((lead & 0xF0u) == 0xE0u) { extra = 2; codePoint = lead & 0x0Fu; lowestLegal = 0x800u; }
            else if ((lead & 0xF8u) == 0xF0u) { extra = 3; codePoint = lead & 0x07u; lowestLegal = 0x10000u; }
            else { return std::nullopt; }

            if (text.size() <= extra) { return std::nullopt; }
            for (std::size_t k = 1; k <= extra; ++k)
            {
                const auto cont = static_cast<std::uint8_t>(text[k]);
                if ((cont & 0xC0u) != 0x80u) { return std::nullopt; }
                codePoint = (codePoint << 6) | (cont & 0x3Fu);
            }
            if (codePoint < lowestLegal) { return std::nullopt; }
            if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) { return std::nullopt; }
            if (codePoint > 0x10FFFFu) { return std::nullopt; }
            return codePoint;
        }

        /// A `charcs` is a UTF-16 code unit, so a `.cnj` character value has to be a Unicode
        /// scalar inside the Basic Multilingual Plane. A surrogate half is not a character, and a
        /// value above U+FFFF cannot be stored in one code unit -- both used to be cast straight
        /// through and would have produced a glyph nothing could ever match.
        SharpRuntime::charcs RequireBmpScalar(std::int64_t value, const std::string& what,
                                              const char* field)
        {
            if (value >= 0xD800 && value <= 0xDFFF)
            {
                Fail(what, std::string(field) + " is U+" + std::to_string(value) +
                               ", a UTF-16 surrogate half rather than a character.");
            }
            return static_cast<SharpRuntime::charcs>(value);
        }

        Rectangle ReadRectangle(const JsonValue& glyph, const char* member, const std::string& what)
        {
            const JsonValue& array = RequireCnjNumberArray(glyph, member, 4u, what);
            const std::string field = std::string("a glyph's '") + member + "'";
            const std::int64_t x = RequireCnjInteger(&array.arrayValue[0], what + ", " + field + " x",
                                                     std::numeric_limits<std::int32_t>::min(),
                                                     std::numeric_limits<std::int32_t>::max());
            const std::int64_t y = RequireCnjInteger(&array.arrayValue[1], what + ", " + field + " y",
                                                     std::numeric_limits<std::int32_t>::min(),
                                                     std::numeric_limits<std::int32_t>::max());
            const std::int64_t w = RequireCnjInteger(&array.arrayValue[2], what + ", " + field + " width",
                                                     std::numeric_limits<std::int32_t>::min(),
                                                     std::numeric_limits<std::int32_t>::max());
            const std::int64_t h = RequireCnjInteger(&array.arrayValue[3], what + ", " + field + " height",
                                                     std::numeric_limits<std::int32_t>::min(),
                                                     std::numeric_limits<std::int32_t>::max());
            return Rectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
                             static_cast<int>(h));
        }
    }

    double RequireCnjFiniteNumber(const JsonValue* value, const std::string& what)
    {
        if (value == nullptr) { Fail(what, "is missing."); }
        if (value->type != JsonType::Number) { Fail(what, "is not a number."); }
        if (!std::isfinite(value->numberValue))
        {
            Fail(what, "is " + Describe(value->numberValue) + ", which is not a finite number.");
        }
        return value->numberValue;
    }

    float RequireCnjSingle(const JsonValue* value, const std::string& what)
    {
        const double number = RequireCnjFiniteNumber(value, what);
        // Checked before narrowing rather than after: a double above FLT_MAX narrows to an
        // infinity, and every consumer of a spacing or a bearing would then propagate it.
        if (number > static_cast<double>(std::numeric_limits<float>::max()) ||
            number < -static_cast<double>(std::numeric_limits<float>::max()))
        {
            Fail(what, "is " + Describe(number) +
                           ", outside the range a 32-bit float can represent.");
        }
        return static_cast<float>(number);
    }

    std::int64_t RequireCnjInteger(const JsonValue* value, const std::string& what,
                                   std::int64_t minInclusive, std::int64_t maxInclusive)
    {
        const double number = RequireCnjFiniteNumber(value, what);
        if (number != std::trunc(number))
        {
            Fail(what, "is " + Describe(number) + ", which is not a whole number.");
        }
        // Compared as doubles before the cast: converting a double outside int64's range to
        // int64 is undefined behaviour, so the range test cannot be done on the result.
        if (number < static_cast<double>(minInclusive) ||
            number > static_cast<double>(maxInclusive))
        {
            Fail(what, "is " + Describe(number) + ", outside the accepted range " +
                           std::to_string(minInclusive) + " to " + std::to_string(maxInclusive) +
                           ".");
        }
        return static_cast<std::int64_t>(number);
    }

    const JsonValue& RequireCnjNumberArray(const JsonValue& object, const char* member,
                                            std::size_t count, const std::string& what)
    {
        const JsonValue* array = object.FindMember(member);
        if (array == nullptr)
        {
            Fail(what, std::string("has no '") + member + "' array.");
        }
        if (array->type != JsonType::Array)
        {
            Fail(what, std::string("'") + member + "' is not an array.");
        }
        if (array->arrayValue.size() != count)
        {
            Fail(what, std::string("'") + member + "' has " +
                           std::to_string(array->arrayValue.size()) + " element(s); exactly " +
                           std::to_string(count) + " are required.");
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            if (array->arrayValue[i].type != JsonType::Number)
            {
                Fail(what, std::string("'") + member + "' element " + std::to_string(i) +
                               " is not a number.");
            }
        }
        return *array;
    }

    std::optional<std::array<std::uint8_t, 3>> ReadCnjColorKey(const JsonValue& root,
                                                                const std::string& what)
    {
        if (root.FindMember("colorKey") == nullptr) { return std::nullopt; }
        const JsonValue& array = RequireCnjNumberArray(root, "colorKey", 3u, what);
        std::array<std::uint8_t, 3> key{};
        static const char* kChannels[3] = {"red", "green", "blue"};
        for (std::size_t i = 0; i < 3u; ++i)
        {
            key[i] = static_cast<std::uint8_t>(RequireCnjInteger(
                &array.arrayValue[i], what + ", colorKey " + kChannels[i], 0, 255));
        }
        return key;
    }

    CnjTexture3DDescription ReadCnjTexture3DDescription(const JsonValue& root,
                                                         const std::string& what)
    {
        CnjTexture3DDescription description;
        description.width = static_cast<std::uint32_t>(RequireCnjInteger(
            root.FindMember("width"), what + ", 'width'", 1, CnjMaxTextureDimension));
        description.height = static_cast<std::uint32_t>(RequireCnjInteger(
            root.FindMember("height"), what + ", 'height'", 1, CnjMaxTextureDimension));
        description.depth = static_cast<std::uint32_t>(RequireCnjInteger(
            root.FindMember("depth"), what + ", 'depth'", 1, CnjMaxTextureDimension));

        // Each factor is at most 65536 and the product is taken in std::uint64_t, so 65536^3 * 4
        // -- about 2^50 -- provably cannot overflow. The remaining question is whether it fits
        // this platform's std::size_t, which on a 32-bit host it need not.
        const std::uint64_t texels = static_cast<std::uint64_t>(description.width) *
                                      description.height * description.depth;
        description.expectedByteCount = texels * 4u;
        if (description.expectedByteCount >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        {
            Fail(what, "declares " + std::to_string(description.width) + "x" +
                           std::to_string(description.height) + "x" +
                           std::to_string(description.depth) + ", which needs " +
                           std::to_string(description.expectedByteCount) +
                           " Rgba8 bytes -- more than this platform can address.");
        }

        const JsonValue* data = root.FindMember("data");
        if (data == nullptr || data->type != JsonType::String || data->stringValue.empty())
        {
            Fail(what, "has no non-empty 'data' field naming a raw pixel sidecar.");
        }
        description.dataFile = data->stringValue;
        return description;
    }

    CnjSpriteFontDescription ReadCnjSpriteFontDescription(const JsonValue& root,
                                                           const std::string& what)
    {
        CnjSpriteFontDescription font;

        const JsonValue* texture = root.FindMember("texture");
        if (texture == nullptr || texture->type != JsonType::String ||
            texture->stringValue.empty())
        {
            Fail(what, "has no non-empty 'texture' field naming its atlas.");
        }
        font.textureName = texture->stringValue;

        // lineSpacing and spacing are optional, matching what the runtime reader has always
        // accepted -- a font with neither is degenerate but not malformed. Present and
        // unreadable is a different claim, and is refused.
        if (const JsonValue* lineSpacing = root.FindMember("lineSpacing"); lineSpacing != nullptr)
        {
            font.lineSpacing = static_cast<std::int32_t>(
                RequireCnjInteger(lineSpacing, what + ", 'lineSpacing'",
                                  std::numeric_limits<std::int32_t>::min(),
                                  std::numeric_limits<std::int32_t>::max()));
        }
        if (const JsonValue* spacing = root.FindMember("spacing"); spacing != nullptr)
        {
            font.spacing = RequireCnjSingle(spacing, what + ", 'spacing'");
        }

        // Present and of the wrong type is refused rather than ignored. `null` is the one value
        // that means "no substitute character", because that is what a generator writes when it
        // has none; a number or an empty string is a document that meant something the reader
        // cannot honour, and silently producing a font with no fallback is how a later
        // MeasureString on an unmapped character becomes the visible symptom.
        if (const JsonValue* defaultCharacter = root.FindMember("defaultCharacter");
            defaultCharacter != nullptr && defaultCharacter->type != JsonType::Null)
        {
            if (defaultCharacter->type != JsonType::String)
            {
                Fail(what, "'defaultCharacter' is not a string.");
            }
            if (defaultCharacter->stringValue.empty())
            {
                Fail(what, "'defaultCharacter' is an empty string; omit the field, or use null, to "
                           "say the font has no substitute character.");
            }
            const std::optional<std::uint32_t> code =
                FirstCodePoint(defaultCharacter->stringValue);
            if (!code.has_value())
            {
                Fail(what, "'defaultCharacter' does not begin with a valid UTF-8 character.");
            }
            if (*code > 0xFFFFu)
            {
                Fail(what, "'defaultCharacter' is U+" + std::to_string(*code) +
                               ", outside the Basic Multilingual Plane a single UTF-16 code unit "
                               "can hold.");
            }
            font.defaultCharacter = static_cast<SharpRuntime::charcs>(*code);
        }

        const JsonValue* glyphs = root.FindMember("glyphs");
        if (glyphs == nullptr || glyphs->type != JsonType::Array || glyphs->arrayValue.empty())
        {
            Fail(what, "has no non-empty 'glyphs' array.");
        }
        font.glyphs.reserve(glyphs->arrayValue.size());
        for (std::size_t i = 0; i < glyphs->arrayValue.size(); ++i)
        {
            const JsonValue& glyph = glyphs->arrayValue[i];
            const std::string glyphWhat = what + ", glyph " + std::to_string(i);
            if (glyph.type != JsonType::Object) { Fail(glyphWhat, "is not an object."); }

            CnjSpriteFontGlyph out;
            out.character = RequireBmpScalar(
                RequireCnjInteger(glyph.FindMember("char"), glyphWhat + " 'char'", 0, 0xFFFF),
                glyphWhat, "'char'");
            out.source = ReadRectangle(glyph, "source", glyphWhat);
            out.crop = ReadRectangle(glyph, "crop", glyphWhat);

            const JsonValue& kerning = RequireCnjNumberArray(glyph, "kerning", 3u, glyphWhat);
            out.kerning = Vector3(
                RequireCnjSingle(&kerning.arrayValue[0], glyphWhat + " kerning left"),
                RequireCnjSingle(&kerning.arrayValue[1], glyphWhat + " kerning advance"),
                RequireCnjSingle(&kerning.arrayValue[2], glyphWhat + " kerning right"));

            font.glyphs.push_back(std::move(out));
        }
        return font;
    }
}
