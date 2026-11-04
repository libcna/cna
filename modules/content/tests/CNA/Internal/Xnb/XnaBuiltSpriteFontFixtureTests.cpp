// SPDX-License-Identifier: MS-PL
//
// TQ-033: SpriteFontReader against fonts the genuine XNA 4.0 FontDescriptionProcessor built
// (tests/reference/xna40/differential/font_description*.xnb from
// tests/assets/xna40/source/{control_font,font_spacing,font_regions}.spritefont, Liberation Mono 14).
// Expected values are the files' own fields, decoded by hand per SpriteFontReader.Read
// IL_0000-0063: ReadObject<Texture2D>, then ReadObject<List<Rectangle>> twice (glyph bounds, then
// cropping), ReadObject<List<char>>, ReadInt32 line spacing, ReadSingle spacing,
// ReadObject<List<Vector3>> kerning, and a Boolean-prefixed char default character, passed to the
// SpriteFont constructor (IL_005e) in that order, which stores each list as is (.ctor IL_0000-0043,
// no validation).
//
// The MeasureString expectations are derived by hand from SpriteFont.InternalMeasure
// IL_0000-01a3: per line Y = max(LineSpacing, cropping.Height); the first glyph of a line uses
// max(kerning.X, 0), every later one adds Spacing + the previous kerning.Z; each adds kerning.X +
// kerning.Y; the line ends with + max(last kerning.Z, 0). A character outside the font resolves to
// DefaultCharacter (GetIndexForCharacter IL_0042-0069) or throws ArgumentException (IL_006a-00a1).

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    Bytes ReadDifferentialFixture(const std::string& name)
    {
        const std::string path = "tests/reference/xna40/differential/" + name + ".xnb";
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.good()) << path << " (the tests run from the source root)";
        return Bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(CNA::TestSupport::EmptyServices(), "Content") { RouteLoadsThroughOpenStreamEXT(true); }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class XnaBuiltSpriteFontFixtureTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            manager.setGraphicsDevice(device);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        SpriteFont LoadFixture(const std::string& name)
        {
            manager.files[name] = ReadDifferentialFixture(name);
            return manager.Load<SpriteFont>(name);
        }

        GraphicsDevice device;
        InMemoryContentManager manager;
    };

    std::size_t IndexOf(const SpriteFont& font, const char16_t character)
    {
        const auto& characters = font.getCharactersProperty();
        for (std::size_t i = 0; i < characters.size(); ++i)
            if (characters[i] == character) return i;
        ADD_FAILURE() << "character U+" << std::hex << static_cast<int>(character) << " missing";
        return 0;
    }
}

// control_font.spritefont: ' '..'~', Spacing 0, UseKerning true, no DefaultCharacter.
TEST_F(XnaBuiltSpriteFontFixtureTest, ControlFontMetricsAreTheFilesListsInTheirOrder)
{
    const SpriteFont font = LoadFixture("font_description");

    EXPECT_EQ(font.getLineSpacingProperty(), 22);
    EXPECT_EQ(font.getSpacingProperty(), 0.0f);
    EXPECT_FALSE(font.getDefaultCharacterProperty().has_value());

    const auto& characters = font.getCharactersProperty();
    ASSERT_EQ(characters.size(), 95u);
    for (std::size_t i = 0; i < characters.size(); ++i)
        EXPECT_EQ(characters[i], static_cast<char16_t>(u' ' + i)) << i;
    ASSERT_EQ(font.getGlyphBoundsEXT().size(), 95u);
    ASSERT_EQ(font.getCroppingEXT().size(), 95u);
    ASSERT_EQ(font.getKerningEXT().size(), 95u);

    // Bounds and cropping are two different lists of the same length; a reader that swapped them
    // or read one twice fails every pair below.
    const std::size_t space = IndexOf(font, u' ');
    EXPECT_EQ(font.getGlyphBoundsEXT()[space], Rectangle(125, 17, 1, 1));
    EXPECT_EQ(font.getCroppingEXT()[space], Rectangle(6, 35, 7, 24));
    EXPECT_EQ(font.getKerningEXT()[space], Vector3(0, 1, 10));
    const std::size_t a = IndexOf(font, u'A');
    EXPECT_EQ(a, 33u);
    EXPECT_EQ(font.getGlyphBoundsEXT()[a], Rectangle(82, 36, 11, 13));
    EXPECT_EQ(font.getCroppingEXT()[a], Rectangle(0, 3, 11, 24));
    EXPECT_EQ(font.getKerningEXT()[a], Vector3(0, 12, -1));
    const std::size_t tilde = IndexOf(font, u'~');
    EXPECT_EQ(font.getGlyphBoundsEXT()[tilde], Rectangle(23, 119, 9, 2));
    EXPECT_EQ(font.getCroppingEXT()[tilde], Rectangle(0, 9, 9, 24));
    EXPECT_EQ(font.getKerningEXT()[tilde], Vector3(1, 10, 0));

    // The 128x128 Dxt3 atlas (format 5). CNA keeps the blocks where the renderer transfers Dxt3
    // natively and decodes to Color elsewhere.
    EXPECT_EQ(font.getTextureEXT().getWidthProperty(), 128);
    EXPECT_EQ(font.getTextureEXT().getHeightProperty(), 128);
    EXPECT_EQ(font.getTextureEXT().getLevelCountProperty(), 1);
    const bool keepsDxt3 = device.GetRenderer().LoadsCompressedContentNativelyEXT() &&
        device.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt3));
    EXPECT_EQ(font.getTextureEXT().getFormatProperty(),
              keepsDxt3 ? SurfaceFormat::Dxt3 : SurfaceFormat::Color);
}

TEST_F(XnaBuiltSpriteFontFixtureTest, ControlFontMeasuresWithItsKerningAndCroppingHeight)
{
    const SpriteFont font = LoadFixture("font_description");
    // "AW": 0 + 12 = 12; then 0 + (-1) + 0 + 12 = 23; end + max(-1, 0). Height max(22, 24).
    EXPECT_EQ(font.MeasureString("AW"), Vector2(23, 24));
    // " ": 0 + 1 = 1, end + max(10, 0) = 11 -- the trailing kerning.Z counts.
    EXPECT_EQ(font.MeasureString(" "), Vector2(11, 24));
    // No DefaultCharacter: a character outside the font is ArgumentException.
    EXPECT_THROW((void)font.MeasureString("\t"), System::ArgumentException);
}

// font_spacing.spritefont: the same face, Spacing 4, UseKerning false, DefaultCharacter '*'.
TEST_F(XnaBuiltSpriteFontFixtureTest, SpacingFontCarriesSpacingDefaultCharacterAndUnkernedAdvances)
{
    const SpriteFont font = LoadFixture("font_description_spacing");

    EXPECT_EQ(font.getLineSpacingProperty(), 22);
    EXPECT_EQ(font.getSpacingProperty(), 4.0f);
    ASSERT_TRUE(font.getDefaultCharacterProperty().has_value());
    EXPECT_EQ(*font.getDefaultCharacterProperty(), u'*');
    ASSERT_EQ(font.getCharactersProperty().size(), 95u);
    // UseKerning false: A = 0, B = advance, C = 0 for every glyph.
    EXPECT_EQ(font.getKerningEXT()[IndexOf(font, u'A')], Vector3(0, 11, 0));
    EXPECT_EQ(font.getKerningEXT()[IndexOf(font, u' ')], Vector3(0, 7, 0));
    EXPECT_EQ(font.getCroppingEXT()[IndexOf(font, u'*')], Rectangle(0, 2, 7, 24));

    // "AW": 0 + 11 = 11; then + 4 + 0, + 0 + 11 = 26.
    EXPECT_EQ(font.MeasureString("AW"), Vector2(26, 24));
    // '\t' is not in the font and measures as '*': 0 + 7 = 7.
    EXPECT_EQ(font.MeasureString("\t"), Vector2(7, 24));
}

// font_regions.spritefont: '?' plus 'A'..'Z' plus U+00C0..U+00CF, DefaultCharacter '?'. The
// characters past U+007F are two-byte UTF-8 in the file (BinaryReader.ReadChar).
TEST_F(XnaBuiltSpriteFontFixtureTest, RegionsFontDecodesItsNonAsciiCharactersAndMetrics)
{
    const SpriteFont font = LoadFixture("font_description_regions");

    const auto& characters = font.getCharactersProperty();
    ASSERT_EQ(characters.size(), 43u);
    EXPECT_EQ(characters[0], u'?');
    EXPECT_EQ(characters[1], u'A');
    EXPECT_EQ(characters[26], u'Z');
    for (std::size_t i = 27; i < 43; ++i)
        EXPECT_EQ(characters[i], static_cast<char16_t>(0x00C0 + (i - 27))) << i;
    ASSERT_TRUE(font.getDefaultCharacterProperty().has_value());
    EXPECT_EQ(*font.getDefaultCharacterProperty(), u'?');

    // U+00CD (index 40): its cropping starts one pixel above the cell (Y = -1).
    EXPECT_EQ(font.getGlyphBoundsEXT()[40], Rectangle(1, 20, 9, 17));
    EXPECT_EQ(font.getCroppingEXT()[40], Rectangle(0, -1, 9, 24));
    EXPECT_EQ(font.getKerningEXT()[40], Vector3(1, 9, 1));
    EXPECT_EQ(font.getGlyphBoundsEXT()[0], Rectangle(71, 50, 9, 13));
    EXPECT_EQ(font.getKerningEXT()[0], Vector3(0, 10, 1));
    // 'a' is outside the font: measured as '?', 0 + 10, end + max(1, 0) = 11.
    EXPECT_EQ(font.MeasureString("a"), Vector2(11, 24));
}
