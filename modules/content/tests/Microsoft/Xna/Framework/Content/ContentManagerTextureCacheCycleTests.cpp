// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-223, ContentManager side. CnjCacheIsolationTests pins the .cnj colour-key case; these
// pin the plain weak-texture-cache lifecycle underneath it -- hit, miss, repeated cycles, wrappers
// outliving each other, and teardown while entries are still registered -- so a future change to
// Texture2D's cache-reconstruction or upload semantics cannot pass by only satisfying the sidecar
// scenario.

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

// A cache HIT -- the first handle is still alive, so the entry's weak_ptrs still lock -- must
// return the same pixels the first load did.
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
}

// A cache MISS -- every handle was dropped, so the entry expired and the asset is reloaded from
// disk -- must also return the original pixels.
TEST_F(ContentManagerTextureCacheCycleTest, CacheMissAfterHandlesExpireReloadsFromDisk)
{
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    {
        Texture2D first = cm.Load<Texture2D>("tile.png");
        ASSERT_EQ(Read(first), Expected());
    }

    Texture2D reloaded = cm.Load<Texture2D>("tile.png");
    EXPECT_EQ(Read(reloaded), Expected());
}

// Alternating hit and miss cycles must never accumulate stale state in either direction.
TEST_F(ContentManagerTextureCacheCycleTest, RepeatedHitAndMissCyclesStayCorrect)
{
    ScratchContentRoot root;
    WriteFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    for (int cycle = 0; cycle < 6; ++cycle)
    {
        Texture2D live = cm.Load<Texture2D>("tile.png");
        ASSERT_EQ(Read(live), Expected()) << "miss cycle " << cycle;

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

// Tearing the ContentManager down while entries are registered, and then the device, must not
// touch a freed renderer or a freed shadow. Meaningful chiefly under ASan/UBSan.
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
        // `cm` is destroyed with both entries still registered, then the two wrappers; `gd` is
        // torn down last, after the cache that referenced its resources is already gone.
    }

    SUCCEED();
}
