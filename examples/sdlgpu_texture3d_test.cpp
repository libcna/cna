// SPDX-License-Identifier: MS-PL
// REMED-GFX-099: SDL_GPU Texture3D transfer/subresource regression.
//
// The public CNA/FNA contract is an explicitly authored mip chain: mipMap=true allocates levels,
// while SetData chooses which level receives exact texels. Plain Texture3D does not implicitly
// regenerate the chain from level 0. SDL_GPU sampling is not part of CNA's current Texture3D
// renderer; this test covers the implemented SetData/GetData contract only.
//
// The checks couple byte-exact content with validation-fatal CTest output. They cover:
// - one-mip level 0 and four distinguishable depth planes;
// - a nontrivial partial SetData/GetData box whose surrounding voxels remain unchanged;
// - a canonical 4x4x4 three-level chain, including nonzero mips;
// - repeated upload, SetData/GetData cycles, and forward/reverse mip order;
// - two independent resources alternated A -> B -> A;
// - a 7x5x3 NPOT chain (7x5x3 -> 3x2x1 -> 1x1x1);
// - destruction after a nonzero-mip transfer.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "common/PixelTestGame.hpp"

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

    std::size_t VoxelCount(int width, int height, int depth)
    {
        return static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height)
            * static_cast<std::size_t>(depth);
    }

    std::size_t VoxelIndex(int x, int y, int z, int width, int height)
    {
        return static_cast<std::size_t>(z * width * height + y * width + x);
    }

    std::vector<Color> Solid(int width, int height, int depth, const Color& color)
    {
        return std::vector<Color>(VoxelCount(width, height, depth), color);
    }

    std::vector<Color> DepthPlanePattern(
        int width, int height, const std::vector<Color>& planeColors)
    {
        std::vector<Color> data(
            static_cast<std::size_t>(width * height) * planeColors.size(),
            Color(0, 0, 0, 0));
        for (std::size_t z = 0; z < planeColors.size(); ++z)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                    data[VoxelIndex(x, y, static_cast<int>(z), width, height)] = planeColors[z];
            }
        }
        return data;
    }
}

class SdlGpuTexture3DTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int checkCount_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        ++checkCount_;
        if (ok) ++passCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    }

    void Upload(Texture3D& texture, int level, int width, int height, int depth,
                const std::vector<Color>& data)
    {
        texture.SetData(level, 0, 0, width, height, 0, depth,
                        data.data(), 0, static_cast<int>(data.size()));
    }

    void CheckExact(Texture3D& texture, int level, int width, int height, int depth,
                    const std::vector<Color>& expected, const std::string& label)
    {
        std::vector<Color> got(
            VoxelCount(width, height, depth), Color(17, 19, 23, 29));
        texture.GetData(level, 0, 0, width, height, 0, depth,
                        got.data(), 0, static_cast<int>(got.size()));

        std::size_t mismatch = got.size();
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            if (!SameRgba(got[i], expected[i]))
            {
                mismatch = i;
                break;
            }
        }
        if (mismatch != got.size())
        {
            const auto& want = expected[mismatch];
            const auto& actual = got[mismatch];
            std::printf(
                "       first mismatch at voxel %zu: want=(%d,%d,%d,%d) got=(%d,%d,%d,%d)\n",
                mismatch,
                want.getRProperty(), want.getGProperty(),
                want.getBProperty(), want.getAProperty(),
                actual.getRProperty(), actual.getGProperty(),
                actual.getBProperty(), actual.getAProperty());
        }
        Check(mismatch == got.size(), label);
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
        const Color dark   (  7,  11,  13, 255);

        // One-mip level-0 control and four distinguishable depth planes.
        Texture3D single(device, 4, 4, 4, false, SurfaceFormat::Color);
        Check(single.getWidthProperty() == 4
              && single.getHeightProperty() == 4
              && single.getDepthProperty() == 4
              && single.getLevelCountProperty() == 1
              && single.getFormatProperty() == SurfaceFormat::Color,
              "one-mip 4x4x4 public properties");

        const auto planes = DepthPlanePattern(4, 4, {red, green, blue, yellow});
        single.SetData(planes.data(), static_cast<int>(planes.size()));
        CheckExact(single, 0, 4, 4, 4, planes,
                   "level 0 exact roundtrip preserves z0/z1/z2/z3 plane order");

        const auto oneMipReplacement = Solid(4, 4, 4, cyan);
        single.SetData(oneMipReplacement.data(), static_cast<int>(oneMipReplacement.size()));
        CheckExact(single, 0, 4, 4, 4, oneMipReplacement,
                   "one-mip repeated full-volume upload replaces prior data");

        // Nontrivial partial box: x/y/z are all nonzero and every extent is smaller than the mip.
        const auto base = Solid(4, 4, 4, dark);
        single.SetData(base.data(), static_cast<int>(base.size()));
        const auto patch = DepthPlanePattern(2, 2, {red, green});
        single.SetData(0, 1, 1, 3, 3, 1, 3,
                       patch.data(), 0, static_cast<int>(patch.size()));

        auto expectedPatched = base;
        for (int z = 0; z < 2; ++z)
        {
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    expectedPatched[VoxelIndex(x + 1, y + 1, z + 1, 4, 4)] =
                        patch[VoxelIndex(x, y, z, 2, 2)];
                }
            }
        }
        CheckExact(single, 0, 4, 4, 4, expectedPatched,
                   "partial SetData changes only the intended 2x2x2 box");

        std::vector<Color> partialRead(patch.size(), Color(17, 19, 23, 29));
        single.GetData(0, 1, 1, 3, 3, 1, 3,
                       partialRead.data(), 0, static_cast<int>(partialRead.size()));
        bool partialExact = partialRead.size() == patch.size();
        for (std::size_t i = 0; partialExact && i < patch.size(); ++i)
            partialExact = SameRgba(partialRead[i], patch[i]);
        Check(partialExact, "partial GetData returns the exact 2x2x2 box");

        // Canonical 4x4x4 authored chain: 4x4x4, 2x2x2, 1x1x1.
        Texture3D primary(device, 4, 4, 4, true, SurfaceFormat::Color);
        Check(primary.getWidthProperty() == 4
              && primary.getHeightProperty() == 4
              && primary.getDepthProperty() == 4
              && primary.getLevelCountProperty() == 3,
              "canonical 4x4x4 reports three authored mip levels");

        const auto level0Red = Solid(4, 4, 4, red);
        const auto level1Green = Solid(2, 2, 2, green);
        const auto level2Blue = Solid(1, 1, 1, blue);
        Upload(primary, 0, 4, 4, 4, level0Red);
        Upload(primary, 1, 2, 2, 2, level1Green);
        Upload(primary, 2, 1, 1, 1, level2Blue);
        CheckExact(primary, 0, 4, 4, 4, level0Red,
                   "forward 0->1->2: level 0 exact");
        CheckExact(primary, 1, 2, 2, 2, level1Green,
                   "forward 0->1->2: nonzero level 1 exact");
        CheckExact(primary, 2, 1, 1, 1, level2Blue,
                   "forward 0->1->2: terminal level 2 exact");

        // Repeated upload of the same nonzero mip, with a readback between A and B.
        const auto level1White = Solid(2, 2, 2, white);
        Upload(primary, 1, 2, 2, 2, level1White);
        CheckExact(primary, 1, 2, 2, 2, level1White,
                   "repeated level-1 upload pattern A exact");
        const auto level1Magenta = Solid(2, 2, 2, magenta);
        Upload(primary, 1, 2, 2, 2, level1Magenta);
        CheckExact(primary, 1, 2, 2, 2, level1Magenta,
                   "repeated level-1 upload pattern B replaces A");
        CheckExact(primary, 0, 4, 4, 4, level0Red,
                   "repeated level-1 uploads preserve level 0");
        CheckExact(primary, 2, 1, 1, 1, level2Blue,
                   "repeated level-1 uploads preserve level 2");

        // Required SetData/GetData cycle: level 0, level 1, then level 0 again.
        const auto level0Orange = Solid(4, 4, 4, orange);
        Upload(primary, 0, 4, 4, 4, level0Orange);
        CheckExact(primary, 0, 4, 4, 4, level0Orange,
                   "SetData/GetData cycle: level 0 first");
        const auto level1Cyan = Solid(2, 2, 2, cyan);
        Upload(primary, 1, 2, 2, 2, level1Cyan);
        CheckExact(primary, 1, 2, 2, 2, level1Cyan,
                   "SetData/GetData cycle: level 1");
        const auto level0Purple = Solid(4, 4, 4, purple);
        Upload(primary, 0, 4, 4, 4, level0Purple);
        CheckExact(primary, 0, 4, 4, 4, level0Purple,
                   "SetData/GetData cycle: level 0 again");

        // Explicit 0 -> 1 -> 2 read order after interleaved writes.
        CheckExact(primary, 0, 4, 4, 4, level0Purple,
                   "mip read order 0->1->2: level 0");
        CheckExact(primary, 1, 2, 2, 2, level1Cyan,
                   "mip read order 0->1->2: level 1");
        CheckExact(primary, 2, 1, 1, 1, level2Blue,
                   "mip read order 0->1->2: level 2");

        // Reverse/mixed writes on an independent resource: 2 -> 0 -> 1.
        Texture3D secondary(device, 4, 4, 4, true, SurfaceFormat::Color);
        const auto secondaryLevel2 = Solid(1, 1, 1, orange);
        const auto secondaryLevel0 = Solid(4, 4, 4, yellow);
        const auto secondaryLevel1 = Solid(2, 2, 2, white);
        Upload(secondary, 2, 1, 1, 1, secondaryLevel2);
        Upload(secondary, 0, 4, 4, 4, secondaryLevel0);
        Upload(secondary, 1, 2, 2, 2, secondaryLevel1);
        CheckExact(secondary, 2, 1, 1, 1, secondaryLevel2,
                   "reverse write order 2->0->1 preserves level 2");
        CheckExact(secondary, 0, 4, 4, 4, secondaryLevel0,
                   "reverse write order 2->0->1 preserves level 0");
        CheckExact(secondary, 1, 2, 2, 2, secondaryLevel1,
                   "reverse write order 2->0->1 preserves level 1");

        // Object A -> B -> A: no renderer-global descriptor/region state may leak.
        CheckExact(primary, 1, 2, 2, 2, level1Cyan,
                   "resource A->B->A: A");
        CheckExact(secondary, 1, 2, 2, 2, secondaryLevel1,
                   "resource A->B->A: B");
        CheckExact(primary, 1, 2, 2, 2, level1Cyan,
                   "resource A->B->A: A again");

        // NPOT chain: mip dimensions independently clamp to one.
        Texture3D npot(device, 7, 5, 3, true, SurfaceFormat::Color);
        Check(npot.getLevelCountProperty() == 3,
              "NPOT 7x5x3 reports three authored mip levels");
        const auto npotLevel0 = DepthPlanePattern(7, 5, {purple, orange, cyan});
        const auto npotLevel1 = Solid(3, 2, 1, magenta);
        const auto npotLevel2 = Solid(1, 1, 1, white);
        Upload(npot, 0, 7, 5, 3, npotLevel0);
        Upload(npot, 1, 3, 2, 1, npotLevel1);
        Upload(npot, 2, 1, 1, 1, npotLevel2);
        CheckExact(npot, 0, 7, 5, 3, npotLevel0,
                   "NPOT level 0 preserves three depth planes");
        CheckExact(npot, 1, 3, 2, 1, npotLevel1,
                   "NPOT nonzero level 1 has 3x2x1 geometry");
        CheckExact(npot, 2, 1, 1, 1, npotLevel2,
                   "NPOT terminal level has 1x1x1 geometry");

        // Transfer resources can be released with the texture immediately after a completed read.
        {
            Texture3D transient(device, 4, 4, 4, true, SurfaceFormat::Color);
            const auto data = Solid(2, 2, 2, yellow);
            Upload(transient, 1, 2, 2, 2, data);
            CheckExact(transient, 1, 2, 2, 2, data,
                       "destroy-after-nonzero-transfer control");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    SdlGpuTexture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuTexture3DTest game;
    game.Run();
    return game.Result();
}
