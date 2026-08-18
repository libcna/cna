// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-43/CNB-46: first-ever .cnj coverage for Texture3D. Unlike TextureCube (.dds via
// sourceFile), Texture3D has no native passthrough file format at all -- self-contained JSON
// ("width"/"height"/"depth") + a raw RGBA8 binary sidecar ("data"), mirroring Model's own
// vertex/index binary-sidecar convention.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "CNA/RendererTestGate.hpp"
#include "System/NotSupportedException.hpp"

// Lets CNA_RENDERER_IS name identities bare.
using namespace CNA::Testing::Renderers;
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture3D;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_texture3d_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    // 2x2x2 volume, each voxel a distinct solid color so GetData() round-trip can be verified
    // exactly, not just "didn't throw".
    void WriteTwoByTwoByTwoFixture(const std::filesystem::path& root)
    {
        std::vector<std::uint8_t> bytes;
        for (int i = 0; i < 8; ++i)
        {
            bytes.push_back(static_cast<std::uint8_t>(i * 10)); // r
            bytes.push_back(static_cast<std::uint8_t>(i * 20)); // g
            bytes.push_back(static_cast<std::uint8_t>(i * 30)); // b
            bytes.push_back(255);                               // a
        }
        std::ofstream f(root / "volume.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.close();

        WriteFile(root / "volume.cnj", R"({
            "cnjVersion": 1,
            "type": "Texture3D",
            "width": 2,
            "height": 2,
            "depth": 2,
            "data": "volume.bin"
        })");
    }
}

class CnjTexture3DTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

// REMED-CONTENT-004: Texture3D is a documented, renderer-dependent capability -- Headless has no
// real GPU resource of any kind, and Software's Texture3D support is an explicit v1 scope boundary
// (plan_software.md Boundaries). On a renderer that doesn't support it, loading now throws a clean
// System::NotSupportedException (from Texture3D's own constructor) instead of previously silently
// succeeding with all-zero pixel data.
TEST_F(CnjTexture3DTest, LoadsRealCnjFixture)
{
    ScratchContentRoot root;
    WriteTwoByTwoByTwoFixture(root.path());

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
    {
        EXPECT_THROW(cm.Load<std::shared_ptr<Texture3D>>("volume"), System::NotSupportedException);
        return;
    }

    std::shared_ptr<Texture3D> texture = cm.Load<std::shared_ptr<Texture3D>>("volume");

    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidthProperty(), 2);
    EXPECT_EQ(texture->getHeightProperty(), 2);
    EXPECT_EQ(texture->getDepthProperty(), 2);

    std::vector<Color> pixels(8, Color(0, 0, 0, 0));
    // IGL owns real volume pixels but cannot fetch them back: IGL v1.1.1 has no way to attach a 3D
    // texture to a framebuffer, its only readback route (plan_igl.md IGL-17). Everything this test
    // asserts about the CNJ reader itself is checked above; only the round trip needs readback.
    if (CNA_RENDERER_IS(Igl))
    {
        EXPECT_THROW((void)texture->GetData(pixels.data(), 8), System::NotSupportedException);
        return;
    }
    texture->GetData(pixels.data(), 8);
    for (int i = 0; i < 8; ++i)
    {
        EXPECT_EQ(pixels[static_cast<std::size_t>(i)],
                  Color(static_cast<std::uint8_t>(i * 10), static_cast<std::uint8_t>(i * 20),
                        static_cast<std::uint8_t>(i * 30), static_cast<std::uint8_t>(255)));
    }
}

TEST_F(CnjTexture3DTest, MismatchedByteCountThrows)
{
    ScratchContentRoot root;
    // 1 byte instead of the required 2*2*2*4 = 32.
    std::ofstream f(root.path() / "bad.bin", std::ios::binary);
    f.put('\0');
    f.close();
    WriteFile(root.path() / "bad.cnj", R"({
        "cnjVersion": 1, "type": "Texture3D", "width": 2, "height": 2, "depth": 2, "data": "bad.bin"
    })");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Texture3D>>("bad"), ContentLoadException);
}

TEST_F(CnjTexture3DTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj", R"({"cnjVersion": 1, "type": "Model"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Texture3D>>("wrong"), ContentLoadException);
}

TEST_F(CnjTexture3DTest, SourceFileRejected)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "Texture3D", "sourceFile": "volume.bin"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Texture3D>>("wrong"), ContentLoadException);
}
