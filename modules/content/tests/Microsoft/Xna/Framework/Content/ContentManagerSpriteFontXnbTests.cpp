// SPDX-License-Identifier: MS-PL
//
// XNB-31: end-to-end M3 milestone test -- content.Load<SpriteFont>("fixture")
// against real, externally-produced .xnb fixtures, going through ContentManager (not a
// standalone parser call). Covers both the uncompressed case (Default.xnb) and, re-using the
// Phase D fixture, the LZX-compressed case (FontCalibri14.xnb) -- M3 requires both a real
// SpriteFont and compressed Texture2D to work together.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

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
    ContentManager cm(CNA::TestSupport::EmptyServices(), "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    SpriteFont font = cm.Load<SpriteFont>("Default");

    EXPECT_EQ(font.getLineSpacingProperty(), 19);
    EXPECT_FLOAT_EQ(font.getSpacingProperty(), 0.0f);
    EXPECT_EQ(font.getCharactersProperty().size(), 95u);
    EXPECT_EQ(font.getCharactersProperty().front(), u' ');
    EXPECT_EQ(font.getCharactersProperty().back(), u'~');
    EXPECT_FALSE(font.getDefaultCharacterProperty().has_value());
    const bool preservesDxt3 = gd.GetRenderer().LoadsCompressedContentNativelyEXT()
        && gd.GetRenderer().IsCompressedTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt3));
    EXPECT_EQ(font.getTextureEXT().getFormatProperty(),
              preservesDxt3 ? SurfaceFormat::Dxt3 : SurfaceFormat::Color);
}

TEST_F(ContentManagerSpriteFontXnbTest, LoadRealLzxCompressedFixtureEndToEnd)
{
    // Real, externally-produced LZX-compressed fixture (MonoGame's own
    // Tests/Interactive/MacOS/TextureScaleColorTest/Content/FontCalibri14.xnb), already vendored
    // for Phase D (XNB-30B). Ties Phase D (decompression) and Phase E (SpriteFontReader)
    // together for the first time through the full ContentManager path, not just a standalone
    // decompress-then-parse-the-table check.
    ContentManager cm(CNA::TestSupport::EmptyServices(), "tests/assets/xnb/monogame/windows/lzx");
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

TEST_F(ContentManagerSpriteFontXnbTest, ReachLoadsAnAuthoredNpotDxtSpriteFontAtlas)
{
    ASSERT_EQ(gd.getGraphicsProfileProperty(), GraphicsProfile::Reach);
    EXPECT_THROW((void)Texture2D(gd, 128, 132, false, SurfaceFormat::Dxt3),
                 System::NotSupportedException);

    ContentManager cm(CNA::TestSupport::EmptyServices(), "tests/assets/xnb/xna40/platformer_hud");
    cm.setGraphicsDevice(gd);
    const SpriteFont font = cm.Load<SpriteFont>("Hud");

    EXPECT_EQ(font.getTextureEXT().getWidthProperty(), 128);
    EXPECT_EQ(font.getTextureEXT().getHeightProperty(), 132);
    EXPECT_EQ(font.getTextureEXT().getFormatProperty(), SurfaceFormat::Color);
    // TQ-033: exact, from the file's own metrics (LineSpacing 24, Spacing 0) and XNA's
    // SpriteFont.InternalMeasure IL_0000-01a3. Kerning (A, B, C) of T I M E : ' ' 0 1 : 5 9 =
    // (0,12,0) (1,4,1) (0,18,0) (1,9,1) (1,3,1) (0,1,5) (1,13,0) (0,6,2) (1,3,1) (1,9,-1) (0,10,1):
    // 12, +0+5=17, +1+18=36, +0+10=46, +1+4=51, +1+1=53, +5+14=72, +0+6=78, +2+4=84, +1+10=95,
    // -1+10=104, end +max(1,0)=105; height max(24, cropping height 27) = 27.
    EXPECT_EQ(font.getLineSpacingProperty(), 24);
    EXPECT_EQ(font.getCharactersProperty().size(), 95u);
    EXPECT_EQ(font.MeasureString("TIME: 01:59"), Microsoft::Xna::Framework::Vector2(105, 27));
}
