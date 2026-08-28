// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/CnjCanonicalRead.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

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

    namespace
    {
        Microsoft::Xna::Framework::CurveLoopType ParseCurveLoopType(
            const std::string& value, const std::string& path)
        {
            using Microsoft::Xna::Framework::CurveLoopType;
            if (value.empty() || value == "Constant") { return CurveLoopType::Constant; }
            if (value == "Cycle") { return CurveLoopType::Cycle; }
            if (value == "CycleOffset") { return CurveLoopType::CycleOffset; }
            if (value == "Oscillate") { return CurveLoopType::Oscillate; }
            if (value == "Linear") { return CurveLoopType::Linear; }
            throw ContentLoadException(
                "Curve .cnj '" + path + "' has an unrecognized CurveLoopType '" + value + "'.");
        }

        Microsoft::Xna::Framework::CurveContinuity ParseCurveContinuity(
            const std::string& value, const std::string& path)
        {
            using Microsoft::Xna::Framework::CurveContinuity;
            if (value.empty() || value == "Smooth") { return CurveContinuity::Smooth; }
            if (value == "Step") { return CurveContinuity::Step; }
            throw ContentLoadException(
                "Curve .cnj '" + path + "' has an unrecognized CurveContinuity '" + value + "'.");
        }

        bool TryReadClipArray(const JsonValue& object, const char* field,
                              std::size_t count, std::vector<float>& output,
                              const std::string& path)
        {
            const JsonValue* value = object.FindMember(field);
            if (value == nullptr) { return false; }
            if (value->type != JsonType::Array || value->arrayValue.size() != count)
            {
                throw ContentLoadException(
                    "AnimationClip .cnj '" + path + "': '" + field + "' must be a " +
                    std::to_string(count) + "-element numeric array.");
            }
            output.clear();
            output.reserve(count);
            for (const JsonValue& element : value->arrayValue)
            {
                if (!element.IsNumber())
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path + "': '" + field +
                        "' has a non-number element.");
                }
                output.push_back(static_cast<float>(element.numberValue));
            }
            return true;
        }
    }

    Microsoft::Xna::Framework::Curve ReadCnjCurve(const JsonValue& root,
                                                   const std::string& path)
    {
        using Microsoft::Xna::Framework::Curve;
        using Microsoft::Xna::Framework::CurveContinuity;
        using Microsoft::Xna::Framework::CurveKey;

        Curve curve;
        if (const JsonValue* preLoop = root.FindMember("preLoop"))
        {
            if (!preLoop->IsString())
            {
                throw ContentLoadException("Curve .cnj '" + path +
                                           "': 'preLoop' must be a string.");
            }
            curve.setPreLoopProperty(ParseCurveLoopType(preLoop->stringValue, path));
        }
        if (const JsonValue* postLoop = root.FindMember("postLoop"))
        {
            if (!postLoop->IsString())
            {
                throw ContentLoadException("Curve .cnj '" + path +
                                           "': 'postLoop' must be a string.");
            }
            curve.setPostLoopProperty(ParseCurveLoopType(postLoop->stringValue, path));
        }

        const JsonValue* keys = root.FindMember("keys");
        if (keys == nullptr || keys->type != JsonType::Array)
        {
            throw ContentLoadException("Curve .cnj '" + path + "' is missing a 'keys' array.");
        }
        for (const JsonValue& keyValue : keys->arrayValue)
        {
            if (!keyValue.IsObject())
            {
                throw ContentLoadException(
                    "Curve .cnj '" + path + "' has a non-object entry in 'keys'.");
            }
            const JsonValue* position = keyValue.FindMember("position");
            const JsonValue* value = keyValue.FindMember("value");
            if (position == nullptr || !position->IsNumber() ||
                value == nullptr || !value->IsNumber())
            {
                throw ContentLoadException(
                    "Curve .cnj '" + path + "' has a key missing numeric 'position'/'value'.");
            }

            float tangentIn = 0.0f;
            if (const JsonValue* tangent = keyValue.FindMember("tangentIn"))
            {
                if (!tangent->IsNumber())
                {
                    throw ContentLoadException("Curve .cnj '" + path +
                                               "': 'tangentIn' must be numeric.");
                }
                tangentIn = static_cast<float>(tangent->numberValue);
            }
            float tangentOut = 0.0f;
            if (const JsonValue* tangent = keyValue.FindMember("tangentOut"))
            {
                if (!tangent->IsNumber())
                {
                    throw ContentLoadException("Curve .cnj '" + path +
                                               "': 'tangentOut' must be numeric.");
                }
                tangentOut = static_cast<float>(tangent->numberValue);
            }
            CurveContinuity continuity = CurveContinuity::Smooth;
            if (const JsonValue* member = keyValue.FindMember("continuity"))
            {
                if (!member->IsString())
                {
                    throw ContentLoadException("Curve .cnj '" + path +
                                               "': 'continuity' must be a string.");
                }
                continuity = ParseCurveContinuity(member->stringValue, path);
            }
            curve.getKeysProperty().Add(CurveKey(
                static_cast<float>(position->numberValue),
                static_cast<float>(value->numberValue), tangentIn, tangentOut, continuity));
        }
        return curve;
    }

    Microsoft::Xna::Framework::Graphics::AnimationClipEXT ReadCnjAnimationClipSidecar(
        const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw ContentLoadException("Cannot open binary file: " + path.string());
        }
        const std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        CNA::Content::Cnb::CnbByteReader reader(
            bytes, "AnimationClip sidecar '" + path.string() + "'");

        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::FromSeconds(reader.ReadF64());
        const std::int32_t trackCount = reader.ReadI32();
        if (trackCount < 0) { reader.Fail("track count is negative."); }
        clip.Tracks.reserve(static_cast<std::size_t>(trackCount));
        for (std::int32_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            BoneTrackEXT track;
            track.BoneIndex = reader.ReadI32();
            const std::int32_t keyCount = reader.ReadI32();
            if (keyCount < 0) { reader.Fail("key count is negative."); }
            track.Keys.reserve(static_cast<std::size_t>(keyCount));
            for (std::int32_t keyIndex = 0; keyIndex < keyCount; ++keyIndex)
            {
                KeyframeEXT key;
                key.Time = System::TimeSpan::FromSeconds(reader.ReadF64());
                const float tx = reader.ReadF32();
                const float ty = reader.ReadF32();
                const float tz = reader.ReadF32();
                key.Translation = Vector3(tx, ty, tz);
                const float qx = reader.ReadF32();
                const float qy = reader.ReadF32();
                const float qz = reader.ReadF32();
                const float qw = reader.ReadF32();
                key.Rotation = Quaternion(qx, qy, qz, qw);
                const float sx = reader.ReadF32();
                const float sy = reader.ReadF32();
                const float sz = reader.ReadF32();
                key.Scale = Vector3(sx, sy, sz);
                track.Keys.push_back(key);
            }
            clip.Tracks.push_back(std::move(track));
        }
        return clip;
    }

    Microsoft::Xna::Framework::Graphics::AnimationClipEXT ReadCnjAnimationClip(
        const JsonValue& root, const std::string& path,
        const CnjSidecarResolver& resolveSidecar)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;

        const JsonValue* clipFile = root.FindMember("clipFile");
        const JsonValue* tracks = root.FindMember("tracks");
        if ((clipFile != nullptr) == (tracks != nullptr))
        {
            throw ContentLoadException(
                "AnimationClip .cnj '" + path +
                "' must have exactly one of 'clipFile' or 'tracks'.");
        }
        if (clipFile != nullptr)
        {
            if (!clipFile->IsString() || clipFile->stringValue.empty())
            {
                throw ContentLoadException(
                    "AnimationClip .cnj '" + path +
                    "' has a non-string or empty 'clipFile'.");
            }
            if (!resolveSidecar)
            {
                throw std::invalid_argument("ReadCnjAnimationClip(): sidecar resolver is empty.");
            }
            return ReadCnjAnimationClipSidecar(resolveSidecar(clipFile->stringValue));
        }

        const JsonValue* duration = root.FindMember("duration");
        if (duration == nullptr || !duration->IsNumber())
        {
            throw ContentLoadException(
                "AnimationClip .cnj '" + path + "' is missing a numeric 'duration' field.");
        }
        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::FromSeconds(duration->numberValue);
        if (const JsonValue* targetSpace = root.FindMember("targetSpace");
            targetSpace != nullptr && targetSpace->IsString() &&
            targetSpace->stringValue == "SceneNode")
        {
            clip.TargetSpace = ClipTargetSpaceEXT::SceneNode;
        }
        if (tracks->type != JsonType::Array)
        {
            throw ContentLoadException(
                "AnimationClip .cnj '" + path + "' has a 'tracks' field that is not an array.");
        }

        std::vector<float> array;
        for (const JsonValue& trackValue : tracks->arrayValue)
        {
            if (!trackValue.IsObject())
            {
                throw ContentLoadException(
                    "AnimationClip .cnj '" + path + "' has a non-object entry in 'tracks'.");
            }
            BoneTrackEXT track;
            const JsonValue* boneIndex = trackValue.FindMember("boneIndex");
            if (boneIndex == nullptr || !boneIndex->IsNumber())
            {
                throw ContentLoadException(
                    "AnimationClip .cnj '" + path +
                    "' has a track missing a numeric 'boneIndex'.");
            }
            track.BoneIndex = static_cast<int>(boneIndex->numberValue);
            const JsonValue* keys = trackValue.FindMember("keys");
            if (keys == nullptr || keys->type != JsonType::Array)
            {
                throw ContentLoadException(
                    "AnimationClip .cnj '" + path + "' has a track missing a 'keys' array.");
            }
            track.Keys.reserve(keys->arrayValue.size());
            for (const JsonValue& keyValue : keys->arrayValue)
            {
                if (!keyValue.IsObject())
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path +
                        "' has a non-object entry in a track's 'keys'.");
                }
                KeyframeEXT key;
                const JsonValue* time = keyValue.FindMember("time");
                if (time == nullptr || !time->IsNumber())
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path +
                        "' has a keyframe missing a numeric 'time'.");
                }
                key.Time = System::TimeSpan::FromSeconds(time->numberValue);
                if (TryReadClipArray(keyValue, "translation", 3u, array, path))
                {
                    key.Translation = Vector3(array[0], array[1], array[2]);
                }
                if (TryReadClipArray(keyValue, "rotation", 4u, array, path))
                {
                    key.Rotation = Quaternion(array[0], array[1], array[2], array[3]);
                }
                if (TryReadClipArray(keyValue, "scale", 3u, array, path))
                {
                    key.Scale = Vector3(array[0], array[1], array[2]);
                }
                track.Keys.push_back(key);
            }
            clip.Tracks.push_back(std::move(track));
        }
        return clip;
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

        // plans/plan_cnb.md CNBF-122: the two rules SpriteFont's own lookup depends on, applied
        // here so the runtime .cnj reader and the .cnj -> .cnb compiler decide identically.
        // EncodeSpriteFontToCnb()/ValidateFont() have always refused both, and nothing on the
        // runtime side did -- so a document the compiler rejected could still be loaded, which is
        // exactly the equivalence CNBF-118 set out to establish and did not finish.
        //
        // Strictly ascending, which also rules out duplicates: SpriteFont binary-searches the
        // character map, so an unsorted or duplicated one does not fail loudly. It returns the
        // wrong glyph, or none.
        for (std::size_t i = 1; i < font.glyphs.size(); ++i)
        {
            if (!(font.glyphs[i - 1u].character < font.glyphs[i].character))
            {
                const bool duplicate =
                    font.glyphs[i - 1u].character == font.glyphs[i].character;
                Fail(what, std::string("glyph ") + std::to_string(i) + " has character U+" +
                               std::to_string(
                                   static_cast<std::uint32_t>(font.glyphs[i].character)) +
                               (duplicate ? ", a duplicate of glyph " : ", which does not follow "
                                                                        "the character of glyph ") +
                               std::to_string(i - 1u) +
                               ". A SpriteFont's characters must be strictly ascending, because "
                               "SpriteFont looks a character up by binary search and an unsorted "
                               "or duplicated map silently returns the wrong glyph.");
            }
        }
        // A defaultCharacter with no glyph is a fallback that cannot be taken: the first
        // unmapped character then has nothing to substitute. SpriteFont's constructor refuses it
        // too, but as a System::ArgumentException from a different layer -- and the compiler,
        // which never constructs one, would otherwise not refuse it at all.
        if (font.defaultCharacter.has_value())
        {
            const bool present =
                std::any_of(font.glyphs.begin(), font.glyphs.end(),
                            [&](const CnjSpriteFontGlyph& glyph)
                            { return glyph.character == *font.defaultCharacter; });
            if (!present)
            {
                Fail(what, "'defaultCharacter' is U+" +
                               std::to_string(
                                   static_cast<std::uint32_t>(*font.defaultCharacter)) +
                               ", which is not one of the font's characters, so the substitution "
                               "it names could never be made.");
            }
        }
        return font;
    }
}
