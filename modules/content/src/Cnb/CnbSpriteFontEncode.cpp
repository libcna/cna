// SPDX-License-Identifier: MS-PL
// EncodeSpriteFontToCnb(): the writing half of the .cnb sprite font codec.
//
// Link boundary: apart from the decoder (CnbSpriteFontCodec.cpp), which every program that loads
// a .cnb font links, so that loading one does not link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <string>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;

namespace CNA::Content::Cnb
{
    namespace
    {
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
    }

    std::vector<std::uint8_t> EncodeSpriteFontToCnb(const CnbSpriteFontData& data,
                                                    const std::string& contentName)
    {
        SpriteFontCodecDetail::ValidateFont(data);
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
        writer.SetMetadata(SpriteFontCodecDetail::kCanonicalName, contentName);
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
        AppendEmbeddedTexture2DChunks(writer, data.atlas, SpriteFontCodecDetail::kLabel);
        return writer.Build();
    }
}
