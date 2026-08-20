// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-32: safe, non-recursive "sourceFile" resolution. A .cnj's "sourceFile" must
// never be able to escape the content root, chain into another .cnj, or cycle back into itself.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    // A tests-only scratch directory tree, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_sourcefile_safety_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    void WriteSolidColorPng(GraphicsDevice& gd, const std::filesystem::path& path, const Color& color)
    {
        std::filesystem::create_directories(path.parent_path());
        Texture2D tex(gd, 2, 2);
        std::vector<Color> pixels(4, color);
        tex.SetData(pixels.data(), 4);
        tex.SaveAsPng(path.string());
    }
}

class CnjSourceFileSafetyTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjSourceFileSafetyTest, InRootSiblingResolvesCorrectly)
{
    ScratchDir scratch;
    const auto root = scratch.path() / "content_root";

    WriteSolidColorPng(gd, root / "ahoj.png", Color(255, 0, 0, 255));
    WriteFile(root / "ahoj.cnj", R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "ahoj.png"})");

    ContentManager cm(nullptr, root.string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("ahoj");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);
    EXPECT_EQ(pixels[0], Color(255, 0, 0, 255));
}

TEST_F(CnjSourceFileSafetyTest, InRootNestedPayloadResolvesCorrectly)
{
    ScratchDir scratch;
    const auto root = scratch.path() / "content_root";

    // .cnj lives directly under root; its payload lives in a nested subdirectory.
    WriteSolidColorPng(gd, root / "textures" / "diffuse.png", Color(0, 255, 0, 255));
    WriteFile(root / "thing.cnj",
              R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "textures/diffuse.png"})");

    ContentManager cm(nullptr, root.string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("thing");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);
    EXPECT_EQ(pixels[0], Color(0, 255, 0, 255));
}

TEST_F(CnjSourceFileSafetyTest, DotDotEscapeIsRejected)
{
    ScratchDir scratch;
    const auto root = scratch.path() / "content_root";
    const auto outside = scratch.path() / "outside_root";

    WriteSolidColorPng(gd, outside / "secret.png", Color(1, 2, 3, 255));
    std::filesystem::create_directories(root);
    WriteFile(root / "evil.cnj",
              R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "../outside_root/secret.png"})");

    ContentManager cm(nullptr, root.string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Texture2D>("evil"), ContentLoadException);
}

TEST_F(CnjSourceFileSafetyTest, AbsolutePathIsRejected)
{
    ScratchDir scratch;
    const auto root = scratch.path() / "content_root";
    const auto absoluteTarget = scratch.path() / "elsewhere.png";

    WriteSolidColorPng(gd, absoluteTarget, Color(1, 2, 3, 255));
    std::filesystem::create_directories(root);
    WriteFile(root / "evil.cnj",
              R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": ")" + absoluteTarget.string() + R"("})");

    ContentManager cm(nullptr, root.string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Texture2D>("evil"), ContentLoadException);
}

TEST_F(CnjSourceFileSafetyTest, CnjChainIsRejected)
{
    ScratchDir scratch;
    const auto root = scratch.path() / "content_root";

    WriteFile(root / "other.cnj", R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "real.png"})");
    WriteFile(root / "chain.cnj", R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "other.cnj"})");

    ContentManager cm(nullptr, root.string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Texture2D>("chain"), ContentLoadException);
}

TEST_F(CnjSourceFileSafetyTest, SelfCycleViaExtensionlessNameIsRejected)
{
    ScratchDir scratch;
    const auto root = scratch.path() / "content_root";

    // "loop.cnj" points sourceFile at "loop" (no extension) -- the normal resolver would pick
    // the sibling "loop.cnj" (itself) over any other candidate, cycling forever if not rejected.
    WriteFile(root / "loop.cnj", R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "loop"})");

    ContentManager cm(nullptr, root.string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Texture2D>("loop"), ContentLoadException);
}
