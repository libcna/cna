// SPDX-License-Identifier: MS-PL
//
// plan_cnb.md CNB-6: proves ContentManager::ResolveAssetPath tries ".cnb" before any
// reader-declared native extension (CNB-4), without needing any reader to understand .cnb
// content yet -- Texture2D's underlying decoder sniffs real image bytes regardless of the
// file's extension, so writing a real PNG's bytes to a "*.cnb" path is enough to prove which
// candidate path the resolver actually picked, using pixel color as the observable signal.

#include <cstdint>
#include <filesystem>
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
                   / ("cna_cnb_resolver_order_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    void WriteSolidColorPng(GraphicsDevice& gd, const std::filesystem::path& path, const Color& color)
    {
        Texture2D tex(gd, 2, 2);
        std::vector<Color> pixels(4, color);
        tex.SetData(pixels.data(), 4);
        tex.SaveAsPng(path.string());
    }
}

class CnbResolverOrderTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnbResolverOrderTest, OnlyNativeFileStillResolvesUnchanged)
{
    ScratchContentRoot root;
    WriteSolidColorPng(gd, root.path() / "foo.png", Color(0, 0, 255, 255));

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("foo");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);
    EXPECT_EQ(pixels[0], Color(0, 0, 255, 255));
}

TEST_F(CnbResolverOrderTest, OnlyCnbFileResolvesViaExtension)
{
    ScratchContentRoot root;
    WriteSolidColorPng(gd, root.path() / "foo.cnb", Color(255, 0, 0, 255));

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("foo");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);
    EXPECT_EQ(pixels[0], Color(255, 0, 0, 255));
}

TEST_F(CnbResolverOrderTest, CnbTakesPriorityOverNativeFileOfSameName)
{
    ScratchContentRoot root;
    WriteSolidColorPng(gd, root.path() / "foo.png", Color(0, 0, 255, 255));
    WriteSolidColorPng(gd, root.path() / "foo.cnb", Color(255, 0, 0, 255));

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("foo");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);
    EXPECT_EQ(pixels[0], Color(255, 0, 0, 255))
        << ".cnb must win over a same-named native file, per cnb.md's core rule";
}
