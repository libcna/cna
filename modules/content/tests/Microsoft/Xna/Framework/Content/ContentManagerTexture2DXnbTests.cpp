// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-23/24/26: end-to-end milestone test -- content.Load<Texture2D>("fixture")
// against a real, externally-produced .xnb fixture, going through ContentManager (not a
// standalone parser call). This is the M2 milestone goal line.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_xnb_texture2d_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    void CopyRealFixture(const std::filesystem::path& destDir, const std::string& destName)
    {
        // Real, externally-produced fixture (MonoGame's own Tests/Assets/Textures/white-1.xnb),
        // vendored at tests/assets/xnb/monogame/windows/uncompressed/ (plans/plan_xnb.md XNB-17A).
        std::error_code ec;
        std::filesystem::copy_file(
            "tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb",
            destDir / destName, ec);
        ASSERT_FALSE(ec) << "Real .xnb fixture not found relative to CWD: " << ec.message();
    }

    class ContentManagerTexture2DXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(ContentManagerTexture2DXnbTest, LoadRealMonoGameFixtureEndToEnd)
{
    ScratchContentRoot root;
    CopyRealFixture(root.path(), "white-1.xnb");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D texture = cm.Load<Texture2D>("white-1");

    EXPECT_EQ(texture.getWidthProperty(), 1);
    EXPECT_EQ(texture.getHeightProperty(), 1);
    EXPECT_EQ(texture.getFormatProperty(), SurfaceFormat::Color);

    Color pixel(0, 0, 0, 0);
    texture.GetData(&pixel, 1);
    EXPECT_EQ(pixel.getRProperty(), 0xFF);
    EXPECT_EQ(pixel.getGProperty(), 0xFF);
    EXPECT_EQ(pixel.getBProperty(), 0xFF);
    EXPECT_EQ(pixel.getAProperty(), 0xFF);
}

TEST_F(ContentManagerTexture2DXnbTest, LoadRealLzxCompressedFixtureEndToEnd)
{
    // plans/plan_xnb.md XNB-30B: re-runs the same end-to-end milestone as
    // LoadRealMonoGameFixtureEndToEnd, but against a real, externally-produced *LZX-compressed*
    // fixture (MonoGame's own Tests/Interactive/MacOS/SoundTest/Content/Explosion.xnb -- despite
    // the name, its root type-reader is Texture2DReader, confirmed by direct inspection).
    // ContentManager root points straight at the fixture directory; no copy needed since nothing
    // in this test writes to it.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/lzx");
    cm.setGraphicsDevice(gd);

    Texture2D texture = cm.Load<Texture2D>("Explosion");

    EXPECT_EQ(texture.getWidthProperty(), 64);
    EXPECT_EQ(texture.getHeightProperty(), 64);
    EXPECT_EQ(texture.getFormatProperty(), SurfaceFormat::Color);

    std::vector<Color> pixels(64 * 64, Color(0, 0, 0, 0));
    texture.GetData(pixels.data(), (int)pixels.size());
    // Not asserting exact pixel values (no independent reference render), but confirming the
    // decoded image is not uniformly blank/garbage -- a real explosion sprite has more than one
    // distinct color across 4096 pixels.
    bool sawNonUniform = false;
    for (std::size_t i = 1; i < pixels.size(); ++i)
    {
        if (pixels[i].getPackedValueProperty() != pixels[0].getPackedValueProperty())
        {
            sawNonUniform = true;
            break;
        }
    }
    EXPECT_TRUE(sawNonUniform);
}

TEST_F(ContentManagerTexture2DXnbTest, LoadCachesTheXnbTextureLikeAnyOtherTexture)
{
    ScratchContentRoot root;
    CopyRealFixture(root.path(), "white-1.xnb");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D first = cm.Load<Texture2D>("white-1");
    Texture2D second = cm.Load<Texture2D>("white-1");

    EXPECT_EQ(first.GetRendererWeak().lock(), second.GetRendererWeak().lock());
}

// plans/plan_xnb.md XNB-47: Texture2D uses its own weak-cache mechanism (textureCache_), a genuinely
// different code path from the generic std::any-based loadedAssets_ cache Unload() also clears --
// confirm Unload() actively evicts it too, not just relying on natural weak-pointer expiry (kept
// `first` alive throughout, specifically so a passing result can't be explained by the old entry
// having merely expired on its own).
TEST_F(ContentManagerTexture2DXnbTest, UnloadClearsTheWeakTextureCache)
{
    ScratchContentRoot root;
    CopyRealFixture(root.path(), "white-1.xnb");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D first = cm.Load<Texture2D>("white-1");
    cm.Unload();
    Texture2D second = cm.Load<Texture2D>("white-1");

    EXPECT_NE(first.GetRendererWeak().lock(), second.GetRendererWeak().lock());
}
