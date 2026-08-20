// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-31: end-to-end M3 milestone test -- content.Load<SpriteFont>("fixture")
// against real, externally-produced .xnb fixtures, going through ContentManager (not a
// standalone parser call). Covers both the uncompressed case (Default.xnb) and, re-using the
// Phase D fixture, the LZX-compressed case (FontCalibri14.xnb) -- M3 requires both a real
// SpriteFont and compressed Texture2D to work together.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SpriteFont;

namespace
{
    class ContentManagerSpriteFontXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
            CNA::Internal::Xnb::RegisterSpriteFontXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(ContentManagerSpriteFontXnbTest, LoadRealUncompressedMonoGameFixtureEndToEnd)
{
    // Real, externally-produced fixture (MonoGame's own Tests/Assets/Fonts/Default.xnb), vendored
    // at tests/assets/xnb/monogame/windows/uncompressed/ -- its glyph atlas happens to be
    // Dxt3-compressed, so this also exercises Texture2DReader's compressed-format path via
    // SpriteFontReader's nested ReadObject<Texture2D>() call.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    SpriteFont font = cm.Load<SpriteFont>("Default");

    EXPECT_EQ(font.getLineSpacingProperty(), 19);
    EXPECT_FLOAT_EQ(font.getSpacingProperty(), 0.0f);
    EXPECT_EQ(font.getCharactersProperty().size(), 95u);
    EXPECT_EQ(font.getCharactersProperty().front(), u' ');
    EXPECT_EQ(font.getCharactersProperty().back(), u'~');
    EXPECT_FALSE(font.getDefaultCharacterProperty().has_value());
}

TEST_F(ContentManagerSpriteFontXnbTest, LoadRealLzxCompressedFixtureEndToEnd)
{
    // Real, externally-produced LZX-compressed fixture (MonoGame's own
    // Tests/Interactive/MacOS/TextureScaleColorTest/Content/FontCalibri14.xnb), already vendored
    // for Phase D (plans/plan_xnb.md XNB-30B). Ties Phase D (decompression) and Phase E (SpriteFontReader)
    // together for the first time through the full ContentManager path, not just a standalone
    // decompress-then-parse-the-table check.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/lzx");
    cm.setGraphicsDevice(gd);

    SpriteFont font = cm.Load<SpriteFont>("FontCalibri14");

    // No independent reference render to check exact metrics against, but a wrong LZX
    // decompression or a wrong SpriteFontReader byte layout would either throw well before this
    // point (a desynced 7-bit-encoded count/string read almost never parses as a well-formed
    // object graph) or leave the font unable to measure ordinary printable text -- neither
    // happened: a real, non-empty glyph set was recovered and can actually be used.
    EXPECT_GT(font.getCharactersProperty().size(), 0u);
    EXPECT_GT(font.MeasureString(std::string("Hello")).X, 0.0f);
}
