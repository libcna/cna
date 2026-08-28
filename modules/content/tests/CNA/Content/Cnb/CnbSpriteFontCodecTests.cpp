// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-102: the SpriteFont schema.
//
// A SpriteFont is an atlas plus four parallel tables, and the interesting failures are all in the
// relationships between them rather than in any one table: arrays that disagree in length, a
// character map that is not sorted (which SpriteFont binary-searches, so it fails SILENTLY), and a
// default character that is not in the font. Those are what this file is mostly about.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbSpriteFontData;
using CNA::Content::Cnb::DecodeSpriteFontFromCnb;
using CNA::Content::Cnb::EncodeSpriteFontToCnb;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    /// A three-glyph font over a 4x4 atlas: small enough to write out, big enough that ordering,
    /// per-glyph pairing and the atlas all have something to get wrong.
    CnbSpriteFontData MakeFont()
    {
        CnbSpriteFontData data;
        data.atlas = CNA::Content::Cnb::MakeRgba8Texture2DData(
            4u, 4u, std::vector<std::uint8_t>(4u * 4u * 4u, 0x5Au));
        data.glyphBounds = {Rectangle(0, 0, 2, 3), Rectangle(2, 0, 1, 3), Rectangle(0, 3, 3, 1)};
        data.cropping = {Rectangle(0, 1, 2, 3), Rectangle(1, 0, 1, 3), Rectangle(0, 0, 3, 1)};
        data.kerning = {Vector3(0.0f, 2.0f, 0.5f), Vector3(-1.0f, 1.0f, 0.0f),
                        Vector3(0.25f, 3.0f, -0.25f)};
        data.characters = {u'A', u'B', u'z'};
        data.lineSpacing = 7;
        data.spacing = 1.5f;
        data.defaultCharacter = u'B';
        return data;
    }

    class ScratchRoot
    {
    public:
        ScratchRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_font_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~ScratchRoot() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
        ScratchRoot(const ScratchRoot&) = delete;
        ScratchRoot& operator=(const ScratchRoot&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        std::filesystem::path path_;
    };
}

TEST(CnbSpriteFontCodecTest, AFontRoundTripsWithItsAtlasAndEveryTable)
{
    const CnbSpriteFontData source = MakeFont();
    const std::vector<std::uint8_t> bytes = EncodeSpriteFontToCnb(source, "Fonts/ui");
    const CnbDocument document = CnbDocument::Parse(bytes, "ui.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::SpriteFont);
    EXPECT_EQ(document.AssetSchemaVersion(), CNA::Content::Cnb::CnbSpriteFontSchemaVersion);
    EXPECT_EQ(document.Metadata().assetTypeName, "Microsoft.Xna.Framework.Graphics.SpriteFont");

    const CnbSpriteFontData decoded = DecodeSpriteFontFromCnb(document);
    EXPECT_EQ(decoded.characters, source.characters);
    EXPECT_EQ(decoded.lineSpacing, source.lineSpacing);
    EXPECT_FLOAT_EQ(decoded.spacing, source.spacing);
    ASSERT_TRUE(decoded.defaultCharacter.has_value());
    EXPECT_EQ(*decoded.defaultCharacter, u'B');

    ASSERT_EQ(decoded.glyphBounds.size(), 3u);
    for (std::size_t i = 0; i < 3u; ++i)
    {
        EXPECT_EQ(decoded.glyphBounds[i], source.glyphBounds[i]) << "glyph " << i;
        EXPECT_EQ(decoded.cropping[i], source.cropping[i]) << "glyph " << i;
        EXPECT_FLOAT_EQ(decoded.kerning[i].X, source.kerning[i].X) << "glyph " << i;
        EXPECT_FLOAT_EQ(decoded.kerning[i].Y, source.kerning[i].Y) << "glyph " << i;
        EXPECT_FLOAT_EQ(decoded.kerning[i].Z, source.kerning[i].Z) << "glyph " << i;
    }

    // The atlas travels in the same file, with the layout a standalone Texture2D would use.
    EXPECT_EQ(decoded.atlas.width, 4u);
    EXPECT_EQ(decoded.atlas.height, 4u);
    EXPECT_EQ(decoded.atlas.faceCount, 1u);
    ASSERT_EQ(decoded.atlas.representations.size(), 1u);
    EXPECT_EQ(decoded.atlas.representations[0].levels[0],
              source.atlas.representations[0].levels[0]);
    EXPECT_EQ(document.FindAll(CNA::Content::Cnb::CnbTextureChunk::Payload).size(), 1u);
}

TEST(CnbSpriteFontCodecTest, AFontWithNoDefaultCharacterRoundTripsAsAbsentRatherThanZero)
{
    // std::nullopt and U+0000 are different things: one throws on a missing glyph, the other
    // renders a NUL. A boolean presence flag is the only way to tell them apart in the bytes.
    CnbSpriteFontData source = MakeFont();
    source.defaultCharacter = std::nullopt;
    const std::vector<std::uint8_t> bytes = EncodeSpriteFontToCnb(source);
    const CnbSpriteFontData decoded =
        DecodeSpriteFontFromCnb(CnbDocument::Parse(bytes, "nodefault.cnb"));
    EXPECT_FALSE(decoded.defaultCharacter.has_value());
}

TEST(CnbSpriteFontCodecTest, TheParallelArraysMustAllAgreeInLength)
{
    // SpriteFont's own constructor requires this, and a shorter array would be an out-of-range
    // read at render time rather than a load error.
    CnbSpriteFontData shortBounds = MakeFont();
    shortBounds.glyphBounds.pop_back();
    EXPECT_THROW((void)EncodeSpriteFontToCnb(shortBounds), ContentLoadException);

    CnbSpriteFontData shortCropping = MakeFont();
    shortCropping.cropping.pop_back();
    EXPECT_THROW((void)EncodeSpriteFontToCnb(shortCropping), ContentLoadException);

    CnbSpriteFontData shortKerning = MakeFont();
    shortKerning.kerning.pop_back();
    EXPECT_THROW((void)EncodeSpriteFontToCnb(shortKerning), ContentLoadException);

    CnbSpriteFontData empty = MakeFont();
    empty.characters.clear();
    empty.glyphBounds.clear();
    empty.cropping.clear();
    empty.kerning.clear();
    EXPECT_THROW((void)EncodeSpriteFontToCnb(empty), ContentLoadException);
}

TEST(CnbSpriteFontCodecTest, AnUnsortedCharacterMapIsRefusedBecauseItWouldFailSilently)
{
    // This is the rule worth having: SpriteFont binary-searches the map, so an unsorted one does
    // not fail loudly -- it returns the wrong glyph. The refusal message says so.
    CnbSpriteFontData unsorted = MakeFont();
    unsorted.characters = {u'z', u'A', u'B'};
    try
    {
        (void)EncodeSpriteFontToCnb(unsorted);
        FAIL() << "an unsorted character map must be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("binary search"), std::string::npos) << e.what();
    }

    // Duplicates are refused for the same reason: one of the two entries is unreachable.
    CnbSpriteFontData duplicated = MakeFont();
    duplicated.characters = {u'A', u'A', u'z'};
    EXPECT_THROW((void)EncodeSpriteFontToCnb(duplicated), ContentLoadException);
}

TEST(CnbSpriteFontCodecTest, ADefaultCharacterOutsideTheFontIsRefused)
{
    CnbSpriteFontData absent = MakeFont();
    absent.defaultCharacter = u'Q';
    EXPECT_THROW((void)EncodeSpriteFontToCnb(absent), ContentLoadException);
}

TEST(CnbSpriteFontCodecTest, TheDecoderRevalidatesRatherThanTrustingTheWriter)
{
    // A file need not have come from this writer. The decoder therefore re-checks the ordering
    // invariant, which no length or checksum test can catch -- both orderings are the same number
    // of well-formed bytes. Built by patching the CHAR chunk of a valid file and repairing the
    // container, which is why this goes through the writer rather than editing bytes in place.
    CnbSpriteFontData source = MakeFont();
    const std::vector<std::uint8_t> good = EncodeSpriteFontToCnb(source);
    ASSERT_NO_THROW((void)DecodeSpriteFontFromCnb(CnbDocument::Parse(good, "good.cnb")));

    CNA::Content::Cnb::CnbByteWriter header;
    header.WriteU32(3u); header.WriteI32(7); header.WriteF32(1.5f);
    header.WriteU32(0u); header.WriteU32(0u); header.WriteU32(0u);
    CNA::Content::Cnb::CnbByteWriter glyphs;
    CNA::Content::Cnb::CnbByteWriter cropping;
    CNA::Content::Cnb::CnbByteWriter kerning;
    for (int i = 0; i < 3; ++i)
    {
        for (int f = 0; f < 4; ++f) { glyphs.WriteI32(f); cropping.WriteI32(f); }
        kerning.WriteF32(0.0f); kerning.WriteF32(1.0f); kerning.WriteF32(0.0f);
    }
    CNA::Content::Cnb::CnbByteWriter characters;
    characters.WriteU32(u'z'); characters.WriteU32(u'A'); characters.WriteU32(u'B'); // unsorted

    CNA::Content::Cnb::CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeId::SpriteFont, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Graphics.SpriteFont", "bad");
    writer.AddChunk(CNA::Content::Cnb::CnbSpriteFontChunk::Header, header.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbSpriteFontChunk::GlyphBounds, glyphs.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbSpriteFontChunk::Cropping, cropping.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbSpriteFontChunk::Kerning, kerning.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::CnbSpriteFontChunk::Characters, characters.Take(),
                    CNA::Content::Cnb::CnbChunkFlags::Mandatory, 4u);
    CNA::Content::Cnb::AppendEmbeddedTexture2DChunks(writer, source.atlas, "SpriteFont");

    EXPECT_THROW((void)DecodeSpriteFontFromCnb(CnbDocument::Parse(writer.Build(), "bad.cnb")),
                 ContentLoadException);
}

TEST(CnbSpriteFontCodecTest, ASpriteFontCnbLoadsThroughContentManagerWithAUsableAtlas)
{
    ScratchRoot root;
    const CnbSpriteFontData source = MakeFont();
    const std::vector<std::uint8_t> bytes = EncodeSpriteFontToCnb(source, "ui");
    std::ofstream out(root.path() / "ui.cnb", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    auto font = cm.Load<Microsoft::Xna::Framework::Graphics::SpriteFont>("ui");

    EXPECT_EQ(font.getLineSpacingProperty(), 7);
    EXPECT_FLOAT_EQ(font.getSpacingProperty(), 1.5f);
    ASSERT_TRUE(font.getDefaultCharacterProperty().has_value());
    EXPECT_EQ(*font.getDefaultCharacterProperty(), u'B');
    EXPECT_EQ(font.getCharactersProperty().size(), 3u);
    EXPECT_EQ(font.getGlyphBoundsEXT()[0], Rectangle(0, 0, 2, 3));
    // The atlas arrived in the same file and became a real texture.
    EXPECT_EQ(font.getTextureEXT().getWidthProperty(), 4);
    EXPECT_EQ(font.getTextureEXT().getHeightProperty(), 4);
}
