// SPDX-License-Identifier: MS-PL
// REMED-GFX-093: Vulkan Texture3D nonzero-mip upload/readback layout regression.
//
// The original Vulkan_Texture3D_Mip_RoundTrip test proved that mip data happened to round-trip,
// but Vulkan validation still reported VUID-vkCmdDraw-None-09600 four times.  The complete
// messages identify copy-layout mismatches, not a sampled descriptor: SetData/GetData copy mip
// levels 1 and 2 while VulkanRenderer::TransitionImageLayout barriers mip level 0.
//
// This Vulkan-only state-cycle test keeps data correctness and validation correctness coupled:
// CMake makes the exact VUID test-fatal, while the checks below cover level 0, nonzero levels,
// different upload orders, repeated uploads, SetData->GetData->SetData, mip A->B->A reads, two
// independent Texture3D objects, a one-level texture, and a non-power-of-two volume.
//
// Vulkan Texture3D shader sampling is not exercised here: Task 863 deliberately implemented
// ShaderEffect Texture3D binding on EasyGL only, and VulkanTexture3DRenderer has no Vulkan sampling
// interface or descriptor path.  GFX-093 is the copy/barrier defect exposed by validation in the
// existing public SetData/GetData path; adding Vulkan sampler3D support is a separate feature.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool SameRgba(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    std::vector<Color> Solid(int width, int height, int depth, const Color& color)
    {
        return std::vector<Color>(
            static_cast<std::size_t>(width * height * depth), color);
    }
}

class VulkanTexture3DMipLayoutTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (!ok) result_ = 1;
    }

    void UploadSolid(Texture3D& texture, int level,
                     int width, int height, int depth, const Color& color)
    {
        auto data = Solid(width, height, depth, color);
        texture.SetData(level, 0, 0, width, height, 0, depth,
                        data.data(), 0, static_cast<int>(data.size()));
    }

    void CheckSolid(Texture3D& texture, int level,
                    int width, int height, int depth, const Color& expected,
                    const std::string& label)
    {
        std::vector<Color> data(
            static_cast<std::size_t>(width * height * depth),
            Color(17, 19, 23, 29));
        texture.GetData(level, 0, 0, width, height, 0, depth,
                        data.data(), 0, static_cast<int>(data.size()));

        std::size_t mismatch = data.size();
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            if (!SameRgba(data[i], expected))
            {
                mismatch = i;
                break;
            }
        }
        if (mismatch != data.size())
        {
            const auto& got = data[mismatch];
            std::printf(
                "       first mismatch at voxel %zu: want=(%d,%d,%d,%d) got=(%d,%d,%d,%d)\n",
                mismatch,
                expected.getRProperty(), expected.getGProperty(),
                expected.getBProperty(), expected.getAProperty(),
                got.getRProperty(), got.getGProperty(),
                got.getBProperty(), got.getAProperty());
        }
        Check(mismatch == data.size(), label);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const Color red    (255,   0,   0, 255);
        const Color green  (  0, 255,   0, 255);
        const Color blue   (  0,   0, 255, 255);
        const Color yellow (255, 255,   0, 255);
        const Color cyan   (  0, 255, 255, 255);
        const Color magenta(255,   0, 255, 255);
        const Color orange (255, 128,   0, 255);
        const Color white  (255, 255, 255, 255);
        const Color purple (128,  32, 192, 255);

        // Canonical 8x8x8 chain: 8^3, 4^3, 2^3, 1^3.
        Texture3D primary(device, 8, 8, 8, true, SurfaceFormat::Color);
        Check(primary.getWidthProperty() == 8
              && primary.getHeightProperty() == 8
              && primary.getDepthProperty() == 8
              && primary.getLevelCountProperty() == 4,
              "8x8x8 reports four legal mip levels");

        // Forward order: level 0 -> level 1 -> level 2 -> level 3.
        UploadSolid(primary, 0, 8, 8, 8, red);
        UploadSolid(primary, 1, 4, 4, 4, green);
        UploadSolid(primary, 2, 2, 2, 2, blue);
        UploadSolid(primary, 3, 1, 1, 1, yellow);
        CheckSolid(primary, 0, 8, 8, 8, red,    "level 0 upload/readback");
        CheckSolid(primary, 1, 4, 4, 4, green,  "nonzero level 1 upload/readback");
        CheckSolid(primary, 2, 2, 2, 2, blue,   "nonzero level 2 upload/readback");
        CheckSolid(primary, 3, 1, 1, 1, yellow, "terminal 1x1x1 upload/readback");

        // Re-upload the same mip, with a readback between A and B.
        UploadSolid(primary, 1, 4, 4, 4, white);
        CheckSolid(primary, 1, 4, 4, 4, white,
                   "level 1 repeated upload A survives");
        UploadSolid(primary, 1, 4, 4, 4, magenta);
        CheckSolid(primary, 1, 4, 4, 4, magenta,
                   "SetData->GetData->SetData level 1 replaces A with B");

        // Deterministic mip A -> B -> A selection through readback.
        CheckSolid(primary, 1, 4, 4, 4, magenta, "mip A->B->A: A (level 1)");
        CheckSolid(primary, 2, 2, 2, 2, blue,    "mip A->B->A: B (level 2)");
        CheckSolid(primary, 1, 4, 4, 4, magenta, "mip A->B->A: A again (level 1)");
        CheckSolid(primary, 0, 8, 8, 8, red,
                   "repeated level 1 operations preserve unrelated level 0");
        CheckSolid(primary, 3, 1, 1, 1, yellow,
                   "repeated level 1 operations preserve unrelated level 3");

        // Reverse/mixed upload order on a second independent resource.
        Texture3D secondary(device, 8, 8, 8, true, SurfaceFormat::Color);
        UploadSolid(secondary, 2, 2, 2, 2, orange);
        UploadSolid(secondary, 0, 8, 8, 8, cyan);
        UploadSolid(secondary, 1, 4, 4, 4, white);
        UploadSolid(secondary, 3, 1, 1, 1, purple);
        CheckSolid(secondary, 2, 2, 2, 2, orange,
                   "mixed order level 2->0->1 preserves level 2");
        CheckSolid(secondary, 0, 8, 8, 8, cyan,
                   "mixed order level 2->0->1 preserves level 0");
        CheckSolid(secondary, 1, 4, 4, 4, white,
                   "mixed order level 2->0->1 preserves level 1");

        // Resource A -> B -> A: state must be per image, never renderer-global.
        CheckSolid(primary,   1, 4, 4, 4, magenta, "resource A->B->A: A");
        CheckSolid(secondary, 1, 4, 4, 4, white,   "resource A->B->A: B");
        CheckSolid(primary,   1, 4, 4, 4, magenta, "resource A->B->A: A again");

        // One-mip control: only level 0 exists and repeated updates remain valid.
        Texture3D single(device, 5, 3, 2, false, SurfaceFormat::Color);
        Check(single.getLevelCountProperty() == 1,
              "mipMap=false reports one level");
        UploadSolid(single, 0, 5, 3, 2, green);
        CheckSolid(single, 0, 5, 3, 2, green,
                   "one-level texture initial upload/readback");
        UploadSolid(single, 0, 5, 3, 2, blue);
        CheckSolid(single, 0, 5, 3, 2, blue,
                   "one-level texture repeated upload/readback");

        // Non-power-of-two chain: 7x5x3 -> 3x2x1 -> 1x1x1.
        Texture3D npot(device, 7, 5, 3, true, SurfaceFormat::Color);
        Check(npot.getLevelCountProperty() == 3,
              "7x5x3 reports three legal mip levels");
        UploadSolid(npot, 0, 7, 5, 3, purple);
        UploadSolid(npot, 1, 3, 2, 1, orange);
        UploadSolid(npot, 2, 1, 1, 1, white);
        CheckSolid(npot, 0, 7, 5, 3, purple,
                   "NPOT level 0 geometry/data");
        CheckSolid(npot, 1, 3, 2, 1, orange,
                   "NPOT nonzero level 1 geometry/data");
        CheckSolid(npot, 2, 1, 1, 1, white,
                   "NPOT terminal level geometry/data");

        // Destruction immediately after a nonzero-level state cycle must remain validation-clean.
        {
            Texture3D transient(device, 4, 4, 4, true, SurfaceFormat::Color);
            UploadSolid(transient, 1, 2, 2, 2, yellow);
            CheckSolid(transient, 1, 2, 2, 2, yellow,
                       "destroy-after-nonzero-update control");
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanTexture3DMipLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanTexture3DMipLayoutTest game;
    game.Run();
    return game.getResult();
}
