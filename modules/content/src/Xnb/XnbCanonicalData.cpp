// SPDX-License-Identifier: MS-PL
// The canonical .xnb decoders of the small types: SpriteFont, Curve, Song and Video. The texture,
// sound and model families have their own translation units (XnbCanonical*.cpp).
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "XnbCanonicalReaderAccess.hpp"
#include "XnbModelGraphReader.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "CNA/Internal/Audio/WavDecoder.hpp"
#include "CNA/Internal/Audio/WavWrapper.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Xnb/XnbArithmetic.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"
#include "XnbCanonicalShared.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        template<typename T, typename ReadElement>
        [[nodiscard]] std::vector<T> ReadList(
            ContentReader& input, const std::string& expectedReader, ReadElement readElement)
        {
            RequireReader(XnbCanonicalReaderAccess::ReadReference(input), expectedReader,
                          input.getAssetNameProperty());
            const std::int32_t count = input.ReadInt32();
            input.CheckCollectionElementCount(count, expectedReader);
            std::vector<T> values;
            values.reserve(static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index)
            {
                values.push_back(readElement());
            }
            return values;
        }
    }

    void RequireReader(const XnbTypeReaderTableEntry& entry, const std::string& expected,
                       const std::string& asset)
    {
        if (entry.normalizedName != expected)
        {
            throw ContentLoadException(
                "'" + asset + "' requires nested ContentTypeReader '" + expected +
                "', but the object references '" + entry.normalizedName + "'.");
        }
        if (entry.version != 0)
        {
            throw ContentLoadException(
                "'" + asset + "' uses reader '" + entry.normalizedName +
                "' at unsupported version (" + std::to_string(entry.version) + ").");
        }
    }

    XnbSpriteFontData DecodeSpriteFontXnbData(
        ContentReader& input, const std::uint32_t maximumTextureDimension)
    {
        RequireReader(
            XnbCanonicalReaderAccess::ReadReference(input),
            "Microsoft.Xna.Framework.Content.Texture2DReader",
            input.getAssetNameProperty());

        XnbSpriteFontData font;
        font.atlas = DecodeTexture2DXnbData(input, maximumTextureDimension);
        font.glyphs = ReadList<Microsoft::Xna::Framework::Rectangle>(
            input,
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]",
            [&input]
            {
                const std::int32_t x = input.ReadInt32();
                const std::int32_t y = input.ReadInt32();
                const std::int32_t width = input.ReadInt32();
                const std::int32_t height = input.ReadInt32();
                return Microsoft::Xna::Framework::Rectangle(x, y, width, height);
            });
        font.cropping = ReadList<Microsoft::Xna::Framework::Rectangle>(
            input,
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]",
            [&input]
            {
                const std::int32_t x = input.ReadInt32();
                const std::int32_t y = input.ReadInt32();
                const std::int32_t width = input.ReadInt32();
                const std::int32_t height = input.ReadInt32();
                return Microsoft::Xna::Framework::Rectangle(x, y, width, height);
            });
        font.characters = ReadList<SharpRuntime::charcs>(
            input, "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]",
            [&input] { return input.ReadChar(); });
        font.lineSpacing = input.ReadInt32();
        font.spacing = input.ReadSingle();
        font.kerning = ReadList<Microsoft::Xna::Framework::Vector3>(
            input,
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]",
            [&input] { return input.ReadVector3(); });
        if (input.ReadBoolean()) { font.defaultCharacter = input.ReadChar(); }
        if (font.glyphs.size() != font.cropping.size() ||
            font.glyphs.size() != font.characters.size() ||
            font.glyphs.size() != font.kerning.size())
        {
            throw ContentLoadException(
                "SpriteFontReader: glyph, cropping, character, and kerning counts differ.");
        }
        return font;
    }

    Microsoft::Xna::Framework::Curve DecodeCurveXnbData(
        ContentReader& input, std::optional<Microsoft::Xna::Framework::Curve> existing)
    {
        using Microsoft::Xna::Framework::Curve;
        using Microsoft::Xna::Framework::CurveContinuity;
        using Microsoft::Xna::Framework::CurveKey;
        using Microsoft::Xna::Framework::CurveLoopType;

        Curve curve = existing.value_or(Curve{});
        curve.setPreLoopProperty(static_cast<CurveLoopType>(input.ReadInt32()));
        curve.setPostLoopProperty(static_cast<CurveLoopType>(input.ReadInt32()));
        const std::int32_t count = input.ReadInt32();
        input.CheckCollectionElementCount(count, "Microsoft.Xna.Framework.Content.CurveReader");
        for (std::int32_t index = 0; index < count; ++index)
        {
            // Read the wire fields in sequence: C++ does not define function-argument evaluation
            // order, while the XNB representation is strictly position, value, tangents, continuity.
            const float position = input.ReadSingle();
            const float value = input.ReadSingle();
            const float tangentIn = input.ReadSingle();
            const float tangentOut = input.ReadSingle();
            const auto continuity = static_cast<CurveContinuity>(input.ReadInt32());
            curve.getKeysProperty().Add(
                CurveKey(position, value, tangentIn, tangentOut, continuity));
        }
        return curve;
    }

    XnbSongData DecodeSongXnbData(ContentReader& input, const bool objectReferences)
    {
        XnbSongData result;
        result.mediaPath = input.ReadString();
        if (objectReferences)
        {
            RequireReader(
                XnbCanonicalReaderAccess::ReadReference(input),
                "Microsoft.Xna.Framework.Content.Int32Reader",
                input.getAssetNameProperty());
        }
        result.durationMs = input.ReadInt32();
        return result;
    }

    XnbVideoData DecodeVideoXnbData(ContentReader& input, const bool objectReferences)
    {
        XnbVideoData result;
        if (!objectReferences)
        {
            // CNA's established runtime reader used direct fields, including in its historical
            // full-container fixtures. Keep that behavior while the pipeline's strict path below
            // accepts FNA's actual ReadObject<T> wire representation.
            result.mediaPath = input.ReadString();
            result.durationMs = input.ReadInt32();
            result.width = input.ReadInt32();
            result.height = input.ReadInt32();
            result.framesPerSecond = input.ReadSingle();
            result.soundtrackType = input.ReadInt32();
            return result;
        }

        RequireReader(
            XnbCanonicalReaderAccess::ReadReference(input),
            "Microsoft.Xna.Framework.Content.StringReader",
            input.getAssetNameProperty());
        result.mediaPath = input.ReadString();
        const auto readInt32Object = [&input]
        {
            RequireReader(
                XnbCanonicalReaderAccess::ReadReference(input),
                "Microsoft.Xna.Framework.Content.Int32Reader",
                input.getAssetNameProperty());
            return input.ReadInt32();
        };
        result.durationMs = readInt32Object();
        result.width = readInt32Object();
        result.height = readInt32Object();
        RequireReader(
            XnbCanonicalReaderAccess::ReadReference(input),
            "Microsoft.Xna.Framework.Content.SingleReader",
            input.getAssetNameProperty());
        result.framesPerSecond = input.ReadSingle();
        result.soundtrackType = readInt32Object();
        return result;
    }
}
