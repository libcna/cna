// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"

#include <algorithm>
#include <limits>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        constexpr const char* kCanonicalName = "Microsoft.Xna.Framework.Graphics.SpriteFont";
        constexpr const char* kLabel = "SpriteFont";

        [[noreturn]] void Fail(const std::string& what)
        {
            throw ContentLoadException(std::string("CNB SpriteFont: ") + what);
        }

        /// The invariants SpriteFont's own constructor requires. Enforced on encode so no writer
        /// in the project can produce a file the reader then refuses, and again on decode because
        /// the file may not have come from this writer.
        void ValidateFont(const CnbSpriteFontData& data)
        {
            const std::size_t count = data.characters.size();
            if (count == 0u) { Fail("a font must define at least one glyph."); }
            if (count > CnbMaxSpriteFontGlyphs)
            {
                Fail("declares " + std::to_string(count) + " glyphs; the ceiling is " +
                     std::to_string(CnbMaxSpriteFontGlyphs) + ".");
            }
            if (data.glyphBounds.size() != count || data.cropping.size() != count ||
                data.kerning.size() != count)
            {
                Fail("the per-glyph arrays disagree: " + std::to_string(count) + " characters, " +
                     std::to_string(data.glyphBounds.size()) + " glyph bounds, " +
                     std::to_string(data.cropping.size()) + " cropping rectangles, " +
                     std::to_string(data.kerning.size()) + " kerning entries.");
            }
            // SpriteFont looks a character up by binary search, so an unsorted map does not fail
            // loudly -- it silently returns the wrong glyph, or none. Strictly ascending also
            // rules out duplicates, which would make one of the two entries unreachable.
            for (std::size_t i = 1; i < count; ++i)
            {
                if (!(data.characters[i - 1u] < data.characters[i]))
                {
                    Fail("the character map is not strictly ascending at index " +
                         std::to_string(i) +
                         "; SpriteFont looks characters up by binary search, so an unsorted map "
                         "silently returns the wrong glyph.");
                }
            }
            if (data.defaultCharacter.has_value() &&
                !std::binary_search(data.characters.begin(), data.characters.end(),
                                    *data.defaultCharacter))
            {
                Fail("the default character is not one of the font's characters.");
            }
            if (data.atlas.faceCount != 1u || data.atlas.depth != 1u)
            {
                Fail("the glyph atlas must be a plain 2D texture.");
            }
        }

        void WriteRectangles(CnbByteWriter& out, const std::vector<Rectangle>& rectangles)
        {
            for (const Rectangle& rectangle : rectangles)
            {
                out.WriteI32(rectangle.X);
                out.WriteI32(rectangle.Y);
                out.WriteI32(rectangle.Width);
                out.WriteI32(rectangle.Height);
            }
        }

        std::vector<Rectangle> ReadRectangles(CnbByteReader& reader, std::uint32_t count,
                                              const char* which)
        {
            const std::uint64_t expected =
                CheckedMultiply(count, CnbSpriteFontRectangleStride, "CNB SpriteFont");
            if (reader.Remaining() != expected)
            {
                reader.Fail(std::string(which) + " holds " + std::to_string(reader.Remaining()) +
                            " bytes; " + std::to_string(count) + " glyphs need " +
                            std::to_string(expected) + ".");
            }
            std::vector<Rectangle> out;
            out.reserve(count);
            for (std::uint32_t i = 0u; i < count; ++i)
            {
                const std::int32_t x = reader.ReadI32();
                const std::int32_t y = reader.ReadI32();
                const std::int32_t w = reader.ReadI32();
                const std::int32_t h = reader.ReadI32();
                out.emplace_back(x, y, w, h);
            }
            reader.RequireExhausted();
            return out;
        }
    }

    std::vector<std::uint8_t> EncodeSpriteFontToCnb(const CnbSpriteFontData& data,
                                                    const std::string& contentName)
    {
        ValidateFont(data);
        const auto count = static_cast<std::uint32_t>(data.characters.size());

        CnbByteWriter header;
        header.WriteU32(count);
        header.WriteI32(data.lineSpacing);
        header.WriteF32(data.spacing);
        header.WriteU32(data.defaultCharacter.has_value() ? 1u : 0u);
        header.WriteU32(data.defaultCharacter.has_value()
                            ? static_cast<std::uint32_t>(*data.defaultCharacter)
                            : 0u);
        header.WriteU32(0u); // flags: reserved, must be zero

        CnbByteWriter glyphs;
        WriteRectangles(glyphs, data.glyphBounds);
        CnbByteWriter cropping;
        WriteRectangles(cropping, data.cropping);

        CnbByteWriter kerning;
        for (const Vector3& entry : data.kerning)
        {
            kerning.WriteF32(entry.X);
            kerning.WriteF32(entry.Y);
            kerning.WriteF32(entry.Z);
        }

        CnbByteWriter characters;
        for (const SharpRuntime::charcs character : data.characters)
        {
            characters.WriteU32(static_cast<std::uint32_t>(character));
        }

        CnbWriter writer(CnbAssetTypeId::SpriteFont, CnbSpriteFontSchemaVersion);
        writer.SetMetadata(kCanonicalName, contentName);
        writer.AddChunk(CnbSpriteFontChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbSpriteFontChunk::GlyphBounds, glyphs.Take(), CnbChunkFlags::Mandatory,
                        4u);
        writer.AddChunk(CnbSpriteFontChunk::Cropping, cropping.Take(), CnbChunkFlags::Mandatory,
                        4u);
        writer.AddChunk(CnbSpriteFontChunk::Kerning, kerning.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbSpriteFontChunk::Characters, characters.Take(),
                        CnbChunkFlags::Mandatory, 4u);
        // The atlas goes in with exactly the layout a standalone Texture2D would use, rather than
        // a second encoding of the same thing (CNBF-102).
        AppendEmbeddedTexture2DChunks(writer, data.atlas, kLabel);
        return writer.Build();
    }

    CnbSpriteFontData DecodeSpriteFontFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::SpriteFont, CnbSpriteFontSchemaVersion);
        const CnbChunkId known[] = {
            CnbSpriteFontChunk::Header,   CnbSpriteFontChunk::GlyphBounds,
            CnbSpriteFontChunk::Cropping, CnbSpriteFontChunk::Kerning,
            CnbSpriteFontChunk::Characters, CnbTextureChunk::Header,
            CnbTextureChunk::Representations, CnbTextureChunk::Payload};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header =
            document.OpenChunk(document.RequireSingle(CnbSpriteFontChunk::Header));
        const std::uint32_t count = header.ReadU32();
        CnbSpriteFontData data;
        data.lineSpacing = header.ReadI32();
        data.spacing = header.ReadF32();
        const std::uint32_t hasDefault = header.ReadU32();
        const std::uint32_t defaultCharacter = header.ReadU32();
        const std::uint32_t flags = header.ReadU32();
        header.RequireExhausted();

        if (count == 0u || count > CnbMaxSpriteFontGlyphs)
        {
            header.Fail("declares " + std::to_string(count) + " glyphs; the range is 1-" +
                        std::to_string(CnbMaxSpriteFontGlyphs) + ".");
        }
        if (flags != 0u)
        {
            header.Fail("sets reserved flag bits; this schema version defines none.");
        }
        if (hasDefault > 1u)
        {
            header.Fail("the default-character presence flag is " + std::to_string(hasDefault) +
                        "; it is a boolean.");
        }
        if (hasDefault == 1u &&
            defaultCharacter > static_cast<std::uint32_t>(std::numeric_limits<SharpRuntime::charcs>::max()))
        {
            header.Fail("the default character " + std::to_string(defaultCharacter) +
                        " is not representable as a UTF-16 code unit.");
        }

        CnbByteReader glyphs =
            document.OpenChunk(document.RequireSingle(CnbSpriteFontChunk::GlyphBounds));
        data.glyphBounds = ReadRectangles(glyphs, count, "GLYP");
        CnbByteReader cropping =
            document.OpenChunk(document.RequireSingle(CnbSpriteFontChunk::Cropping));
        data.cropping = ReadRectangles(cropping, count, "CROP");

        CnbByteReader kerning =
            document.OpenChunk(document.RequireSingle(CnbSpriteFontChunk::Kerning));
        const std::uint64_t kerningBytes =
            CheckedMultiply(count, CnbSpriteFontKerningStride, "CNB SpriteFont");
        if (kerning.Remaining() != kerningBytes)
        {
            kerning.Fail("KERN holds " + std::to_string(kerning.Remaining()) + " bytes; " +
                         std::to_string(count) + " glyphs need " + std::to_string(kerningBytes) +
                         ".");
        }
        data.kerning.reserve(count);
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            const float x = kerning.ReadF32();
            const float y = kerning.ReadF32();
            const float z = kerning.ReadF32();
            data.kerning.emplace_back(x, y, z);
        }
        kerning.RequireExhausted();

        CnbByteReader characters =
            document.OpenChunk(document.RequireSingle(CnbSpriteFontChunk::Characters));
        const std::uint64_t characterBytes =
            CheckedMultiply(count, CnbSpriteFontCharacterStride, "CNB SpriteFont");
        if (characters.Remaining() != characterBytes)
        {
            characters.Fail("CHAR holds " + std::to_string(characters.Remaining()) + " bytes; " +
                            std::to_string(count) + " glyphs need " +
                            std::to_string(characterBytes) + ".");
        }
        data.characters.reserve(count);
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            const std::uint32_t raw = characters.ReadU32();
            if (raw > static_cast<std::uint32_t>(std::numeric_limits<SharpRuntime::charcs>::max()))
            {
                characters.Fail("character " + std::to_string(i) + " is " + std::to_string(raw) +
                                ", which is not a UTF-16 code unit.");
            }
            data.characters.push_back(static_cast<SharpRuntime::charcs>(raw));
        }
        characters.RequireExhausted();

        if (hasDefault == 1u) { data.defaultCharacter = static_cast<SharpRuntime::charcs>(defaultCharacter); }
        data.atlas = ReadEmbeddedTexture2DChunks(document, kLabel);

        // The same invariants the encoder enforces, applied to a file that may not have come from
        // it. Ordering in particular is not something a length check can catch.
        ValidateFont(data);
        return data;
    }
}
