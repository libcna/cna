// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-223, ContentManager side. CnjCacheIsolationTests pins the .cnj colour-key case; these
// pin the XNA ContentManager-owned texture-cache lifecycle underneath it -- hits, repeated loads,
// wrappers outliving each other, and teardown while entries are still registered.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    /// A tests-only scratch content root, unique per instance so parallel/repeated runs never
    /// collide. Mirrors CnjCacheIsolationTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_texture_cache_cycle_test_"
                      + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    const std::vector<Color>& Expected()
    {
        static const std::vector<Color> pixels = {
            Color(255, 0, 0, 255), Color(0, 255, 0, 255),
            Color(0, 0, 255, 255), Color(255, 255, 0, 255),
        };
        return pixels;
    }

    void WriteFixture(GraphicsDevice& gd, const std::filesystem::path& root)
    {
        Texture2D source(gd, 2, 2);
        const auto& pixels = Expected();
        source.SetData(pixels.data(), static_cast<int>(pixels.size()));
        source.SaveAsPng((root / "tile.png").string());
    }

    std::vector<Color> Read(Texture2D& texture)
    {
        std::vector<Color> out(4, Color(0, 0, 0, 0));
        texture.GetData(out.data(), static_cast<int>(out.size()));
        return out;
    }
}

class ContentManagerTextureCacheCycleTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

// A cache hit must return the same pixels and renderer the first load did.
TEST_F(ContentManagerTextureCacheCycleTest, CacheHitReturnsTheSamePixels)
{
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D first = cm.Load<Texture2D>("tile.png");
    ASSERT_EQ(Read(first), Expected());

    Texture2D hit = cm.Load<Texture2D>("tile.png");
    EXPECT_EQ(Read(hit), Expected());
    EXPECT_EQ(first.GetRendererWeak().lock(), hit.GetRendererWeak().lock());
}

// XNA ContentManager strongly owns a loaded asset until Unload, even when every caller drops its
// handle. A later Load must therefore return the original renderer without decoding or uploading
// the texture again.
TEST_F(ContentManagerTextureCacheCycleTest, CacheSurvivesCallerHandlesGoingOutOfScope)
{
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> originalRenderer;
    {
        Texture2D first = cm.Load<Texture2D>("tile.png");
        ASSERT_EQ(Read(first), Expected());
        originalRenderer = first.GetRendererWeak();
    }
    ASSERT_FALSE(originalRenderer.expired());

    Texture2D cached = cm.Load<Texture2D>("tile.png");
    EXPECT_EQ(Read(cached), Expected());
    EXPECT_EQ(originalRenderer.lock(), cached.GetRendererWeak().lock());
}

// Repeated cache hits must never accumulate stale state.
TEST_F(ContentManagerTextureCacheCycleTest, RepeatedLoadsStayCorrect)
{
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    for (int cycle = 0; cycle < 6; ++cycle)
    {
        Texture2D live = cm.Load<Texture2D>("tile.png");
        ASSERT_EQ(Read(live), Expected()) << "load cycle " << cycle;

        Texture2D hit = cm.Load<Texture2D>("tile.png");
        ASSERT_EQ(Read(hit), Expected()) << "hit cycle " << cycle;
    }
}

// An upload through one handle must not reach a second handle obtained from the same cache entry.
TEST_F(ContentManagerTextureCacheCycleTest, UploadThroughOneHandleDoesNotReachAnother)
{
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D keep = cm.Load<Texture2D>("tile.png");
    Texture2D other = cm.Load<Texture2D>("tile.png");

    const std::vector<Color> patch(4, Color(7, 7, 7, 255));
    other.SetData(patch.data(), static_cast<int>(patch.size()));

    EXPECT_EQ(Read(other), patch);
    EXPECT_EQ(Read(keep), Expected());
}

// Tearing the ContentManager down while strong entries are registered, and then the device, must
// not touch a freed renderer or a freed shadow. Meaningful chiefly under ASan/UBSan.
TEST(ContentManagerTextureCacheTeardownTest, TeardownWithLiveCacheEntriesIsClean)
{
    GraphicsDevice gd;
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    {
        ContentManager cm(nullptr, root.path().string());
        cm.setGraphicsDevice(gd);

        Texture2D live = cm.Load<Texture2D>("tile.png");
        Texture2D hit = cm.Load<Texture2D>("tile.png");
        ASSERT_EQ(Read(hit), Expected());
        // `cm` is destroyed with its strong entry still registered, then the two wrappers; `gd`
        // is torn down last, after the cache that referenced its resources is already gone.
    }

    SUCCEED();
}
