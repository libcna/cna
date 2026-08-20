// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-33: a .cnj sidecar's metadata transform (colorKey) must never leak into a
// separately-cached, separately-requested load of the same underlying native file, in either
// load order. Investigation found the specific mechanism the task described -- ApplyColorKey's
// SetData() mutating a texture renderer shared through the weak cache -- does not currently
// reproduce: Texture2D::SetData(const Color*, int) reassigns renderer_/cpuPixels_ to freshly
// created targets rather than mutating the existing shared objects in place, so a .cnj's
// recursively-loaded source texture is already effectively detached before ApplyColorKey runs.
// These tests pin that invariant as a permanent regression guard (including the case that
// actually exercises the weak-cache reuse path, by keeping a live handle so the cached entry's
// weak_ptr can still lock()) -- if a future change to SetData ever starts mutating in place
// instead of reassigning, these tests will catch it.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
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
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_cache_isolation_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    void WriteColorKeyedFixture(GraphicsDevice& gd, const std::filesystem::path& root)
    {
        Texture2D source(gd, 2, 2);
        std::vector<Color> pixels = {
            Color(255, 0, 255, 255), Color(0, 0, 255, 255),
            Color(255, 0, 255, 255), Color(0, 255, 0, 255),
        };
        source.SetData(pixels.data(), 4);
        source.SaveAsPng((root / "ahoj.png").string());

        WriteFile(root / "ahoj.cnj", R"({
            "cnjVersion": 1,
            "type": "Texture2D",
            "sourceFile": "ahoj.png",
            "colorKey": [255, 0, 255]
        })");
    }
}

class CnjCacheIsolationTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjCacheIsolationTest, SidecarLoadedFirstDoesNotCorruptLaterNativeLoad)
{
    ScratchContentRoot root;
    WriteColorKeyedFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D sidecar = cm.Load<Texture2D>("ahoj");
    std::vector<Color> sidecarPixels(4, Color(0, 0, 0, 0));
    sidecar.GetData(sidecarPixels.data(), 4);
    ASSERT_EQ(sidecarPixels[0].getAProperty(), 0) << "sidecar pixel 0 should be color-keyed transparent";

    Texture2D native = cm.Load<Texture2D>("ahoj.png");
    std::vector<Color> nativePixels(4, Color(0, 0, 0, 0));
    native.GetData(nativePixels.data(), 4);
    EXPECT_EQ(nativePixels[0].getAProperty(), 255)
        << "explicit native load must return the original, un-color-keyed pixels";
}

TEST_F(CnjCacheIsolationTest, NativeLoadedFirstWithLiveHandleUnaffectedBySidecar)
{
    ScratchContentRoot root;
    WriteColorKeyedFixture(gd, root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    // Keep a live handle so the weak texture cache's entry for "ahoj.png" can still lock() when
    // the .cnj sidecar recursively loads it below -- this is what actually exercises the
    // cache-reuse code path, not a fresh reload from disk.
    Texture2D nativeHandle = cm.Load<Texture2D>("ahoj.png");

    Texture2D sidecar = cm.Load<Texture2D>("ahoj");
    std::vector<Color> sidecarPixels(4, Color(0, 0, 0, 0));
    sidecar.GetData(sidecarPixels.data(), 4);
    ASSERT_EQ(sidecarPixels[0].getAProperty(), 0) << "sidecar pixel 0 should be color-keyed transparent";

    std::vector<Color> nativePixels(4, Color(0, 0, 0, 0));
    nativeHandle.GetData(nativePixels.data(), 4);
    EXPECT_EQ(nativePixels[0].getAProperty(), 255)
        << "the live native handle must retain its original, un-color-keyed pixels";
}
