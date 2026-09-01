// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-9: tests for CNB-7 (generic sourceFile resolution) and CNB-8 (colorKey wired
// into Texture2DTypeReader as the first real sourceFile consumer).

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
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
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_sourcefile_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

class CnjSourceFileTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(CnjSourceFileTest, ColorKeyMakesMatchingPixelsTransparentOthersUnchanged)
{
    ScratchContentRoot root;

    // 2x2: magenta, blue, magenta, green.
    Texture2D source(gd, 2, 2);
    std::vector<Color> sourcePixels = {
        Color(255, 0, 255, 255),
        Color(0, 0, 255, 255),
        Color(255, 0, 255, 255),
        Color(0, 255, 0, 255),
    };
    source.SetData(sourcePixels.data(), 4);
    source.SaveAsPng((root.path() / "ahoj.png").string());

    WriteFile(root.path() / "ahoj.cnj", R"({
        "cnjVersion": 1,
        "type": "Texture2D",
        "sourceFile": "ahoj.png",
        "colorKey": [255, 0, 255]
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("ahoj");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);

    EXPECT_EQ(pixels[0], Color(255, 0, 255, 0)) << "magenta pixel 0 should become transparent";
    EXPECT_EQ(pixels[1], Color(0, 0, 255, 255)) << "blue pixel 1 should be unchanged";
    EXPECT_EQ(pixels[2], Color(255, 0, 255, 0)) << "magenta pixel 2 should become transparent";
    EXPECT_EQ(pixels[3], Color(0, 255, 0, 255)) << "green pixel 3 should be unchanged";
}

TEST_F(CnjSourceFileTest, NativeFileAloneStillLoadsUnchanged)
{
    ScratchContentRoot root;

    Texture2D source(gd, 2, 2);
    std::vector<Color> sourcePixels(4, Color(0, 128, 255, 255));
    source.SetData(sourcePixels.data(), 4);
    source.SaveAsPng((root.path() / "solo.png").string());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    Texture2D loaded = cm.Load<Texture2D>("solo");

    std::vector<Color> pixels(4, Color(0, 0, 0, 0));
    loaded.GetData(pixels.data(), 4);
    EXPECT_EQ(pixels[0], Color(0, 128, 255, 255));
}

TEST_F(CnjSourceFileTest, MissingSourceFileThrows)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "broken.cnj", R"({
        "cnjVersion": 1,
        "type": "Texture2D",
        "sourceFile": "does_not_exist.png"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    // The missing sourceFile ultimately fails inside the native image decode path. ContentManager
    // exposes that reader failure through its documented ContentLoadException contract.
    EXPECT_THROW(cm.Load<Texture2D>("broken"), ContentLoadException);
}

TEST_F(CnjSourceFileTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;

    WriteFile(root.path() / "wrong.cnj", R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "sourceFile": "ahoj.png"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Texture2D>("wrong"), ContentLoadException);
}
