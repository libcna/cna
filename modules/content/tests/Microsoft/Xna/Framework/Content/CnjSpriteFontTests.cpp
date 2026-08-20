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
#include "System/ArgumentException.hpp"

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

    WriteFile(root.path() / "arial.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "texture": "atlas.png",
        "lineSpacing": 24,
        "spacing": 1.5,
        "defaultCharacter": "?",
        "glyphs": [
            { "char": 65, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] },
            { "char": 63, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] }
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
    EXPECT_EQ(font.getCharactersProperty()[0], u'A');
    EXPECT_EQ(font.getCharactersProperty()[1], u'?');
}

// REMED-GFX-002: a defaultCharacter absent from the glyph list must now be rejected at
// construction time (System::ArgumentException surfaces as a load failure), not silently
// accepted and left to invoke UB the first time MeasureString/DrawString needed the fallback.
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

    EXPECT_THROW(cm.Load<SpriteFont>("badDefault"), System::ArgumentException);
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
