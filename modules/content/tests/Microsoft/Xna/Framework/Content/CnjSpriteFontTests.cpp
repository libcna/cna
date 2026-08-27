// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-12/CNB-13: first-ever test coverage for SpriteFontTypeReader, migrated from
// .font.json to .cnj (CNB-11). No prior test or example anywhere exercised this reader.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_spritefont_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }
}

class CnjSpriteFontTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjSpriteFontTest, LoadsRealCnjFixture)
{
    ScratchContentRoot root;

    Texture2D atlas(gd, 16, 24);
    std::vector<Color> pixels(16 * 24, Color(255, 255, 255, 255));
    atlas.SetData(pixels.data(), static_cast<int>(pixels.size()));
    atlas.SaveAsPng((root.path() / "atlas.png").string());

    // The glyphs are in ascending character order -- U+003F '?' then U+0041 'A'. This fixture
    // used to list 'A' first, which SpriteFont's binary search cannot look up correctly and which
    // EncodeSpriteFontToCnb() has always refused; the shared canonical reader now refuses it too,
    // so the two routes agree (plans/plan_cnb.md CNBF-122).
    WriteFile(root.path() / "arial.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "texture": "atlas.png",
        "lineSpacing": 24,
        "spacing": 1.5,
        "defaultCharacter": "?",
        "glyphs": [
            { "char": 63, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] },
            { "char": 65, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] }
        ]
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    SpriteFont font = cm.Load<SpriteFont>("arial");

    EXPECT_EQ(font.getLineSpacingProperty(), 24);
    EXPECT_FLOAT_EQ(font.getSpacingProperty(), 1.5f);
    ASSERT_TRUE(font.getDefaultCharacterProperty().has_value());
    EXPECT_EQ(*font.getDefaultCharacterProperty(), u'?');
    ASSERT_EQ(font.getCharactersProperty().size(), 2u);
    EXPECT_EQ(font.getCharactersProperty()[0], u'?');
    EXPECT_EQ(font.getCharactersProperty()[1], u'A');
}

// REMED-GFX-002: a defaultCharacter absent from the glyph list must be rejected, not silently
// accepted and left to invoke UB the first time MeasureString/DrawString needed the fallback.
//
// plans/plan_cnb.md CNBF-122 moved the refusal earlier and made it shared. It used to come from
// SpriteFont's constructor as a System::ArgumentException, which the `.cnj` -> `.cnb` compiler --
// which constructs no SpriteFont -- never reached; the document is now refused by the canonical
// reader BOTH routes call, as a ContentLoadException. `CnbCompilerStrictnessTests` asserts the
// compiler's half of exactly this document.
TEST_F(CnjSpriteFontTest, DefaultCharacterAbsentFromGlyphsThrows)
{
    ScratchContentRoot root;

    Texture2D atlas(gd, 16, 24);
    std::vector<Color> pixels(16 * 24, Color(255, 255, 255, 255));
    atlas.SetData(pixels.data(), static_cast<int>(pixels.size()));
    atlas.SaveAsPng((root.path() / "atlas.png").string());

    WriteFile(root.path() / "badDefault.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "texture": "atlas.png",
        "lineSpacing": 24,
        "spacing": 1.5,
        "defaultCharacter": "?",
        "glyphs": [
            { "char": 65, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] }
        ]
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<SpriteFont>("badDefault"), ContentLoadException);
}

// plans/plan_cnb.md CNBF-122: the other half of the runtime side of the equivalence. An unsorted
// or duplicated character map is what SpriteFont's binary search cannot survive, and the runtime
// accepted both while EncodeSpriteFontToCnb() refused them -- so a font that compiled nowhere
// still loaded here.
TEST_F(CnjSpriteFontTest, UnsortedOrDuplicatedGlyphCharactersThrow)
{
    ScratchContentRoot root;

    Texture2D atlas(gd, 16, 24);
    std::vector<Color> pixels(16 * 24, Color(255, 255, 255, 255));
    atlas.SetData(pixels.data(), static_cast<int>(pixels.size()));
    atlas.SaveAsPng((root.path() / "atlas.png").string());

    const std::string prologue = R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "texture": "atlas.png",
        "lineSpacing": 24,
        "spacing": 1.5,
        "glyphs": [)";
    const std::string epilogue = "]}";
    const std::string glyphA =
        R"({ "char": 65, "source": [0,0,16,24], "crop": [0,0,16,24], "kerning": [1,14,1] })";
    const std::string glyphB =
        R"({ "char": 66, "source": [0,0,16,24], "crop": [0,0,16,24], "kerning": [1,14,1] })";

    WriteFile(root.path() / "unsorted.cnj", prologue + glyphB + "," + glyphA + epilogue);
    WriteFile(root.path() / "duplicate.cnj", prologue + glyphA + "," + glyphA + epilogue);
    WriteFile(root.path() / "ordered.cnj", prologue + glyphA + "," + glyphB + epilogue);

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<SpriteFont>("unsorted"), ContentLoadException);
    EXPECT_THROW(cm.Load<SpriteFont>("duplicate"), ContentLoadException);

    // The positive control, so the rule is a boundary rather than a blanket refusal.
    SpriteFont ordered = cm.Load<SpriteFont>("ordered");
    ASSERT_EQ(ordered.getCharactersProperty().size(), 2u);
    EXPECT_EQ(ordered.getCharactersProperty()[0], u'A');
    EXPECT_EQ(ordered.getCharactersProperty()[1], u'B');
}

TEST_F(CnjSpriteFontTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "wrong.cnj", R"({
        "cnjVersion": 1,
        "type": "Model"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<SpriteFont>("wrong"), ContentLoadException);
}

// plans/plan_cnj.md CNB-35: end-to-end proof that the strict envelope/version policy is wired through
// a real built-in reader, not just unit-tested against ParseCnjEnvelope/ValidateCnjEnvelope in
// isolation.
TEST_F(CnjSpriteFontTest, UnsupportedCnjVersionThrowsThroughRealReader)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "future.cnj", R"({
        "cnjVersion": 2,
        "type": "SpriteFont"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<SpriteFont>("future"), ContentLoadException);
}
