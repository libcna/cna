// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-47: commit reproducible RenderTargetCube mipmap coverage.
//
// SOKOL-39 closed the cube half of mip-mapped render targets using only a throwaway, uncommitted
// verification program, at a time before SOKOL-34 (real cube sampling) had landed -- neither is
// acceptable for the plan legend's "implemented and verified" criterion. This is the permanent
// replacement: a real `RenderTargetCube(mipMap=true)`, one face cleared solid, unbound (triggering
// `RegenerateMipmapsIfNeededEXT()`'s `glGenerateMipmap`), then read back through
// `RenderTargetCube::GetData()` at mip levels 1 and 2 -- the same direct-CPU-readback API SOKOL-38
// added, now exercising the mip dimension argument that test never needed.
//
// Method: fill face PositiveX, level 0, with solid green (32x32), unbind, then read back the
// WHOLE of level 1 (16x16) and level 2 (8x8) -- not just a corner texel -- so a genuinely
// GL-incomplete mip chain (undefined storage, only the requested rect written, or a stale/zeroed
// buffer) cannot pass by accident the way a single-pixel check could.
//
// Check A -- LevelCount reports the real 32->16->8->4->2->1 chain (6 levels).
// Check B -- every texel of level 1 (16x16) reads back solid green.
// Check C -- every texel of level 2 (8x8) reads back solid green.
//
// The "preferably ... samples the target through the landed EnvironmentMapEffect path" suggestion
// in SOKOL-47's own acceptance criteria is deliberately not attempted here: envmap3d.glsl (like
// real XNA's own EnvironmentMapEffect) has no explicit mip-bias/LOD control, so a full-screen quad
// sampling through it has no reliable way to force level 1/2 over level 0 -- automatic GLSL LOD
// selection from screen-space derivatives is not a controllable, reproducible test signal. Direct
// `GetData()` readback (this file) is the reproducible, deterministic proof; `Sokol_
// EnvironmentMapEffect_AlphaScaledLerp` already covers level-0 cube sampling through that effect.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 32;
}

class RenderTargetCubeMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    static bool AllGreen(const std::vector<Color>& pixels)
    {
        for (const Color& c : pixels)
        {
            if (c.getRProperty() > 10 || c.getGProperty() < 245 || c.getBProperty() > 10)
                return false;
        }
        return true;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        RenderTargetCube cube(dev, kCubeSize, /*mipMap=*/true, SurfaceFormat::Color,
                              DepthFormat::None);

        check(cube.getLevelCountProperty() == 6,
              "A: LevelCount reports the real 32->16->8->4->2->1 chain");

        // Fill level 0 with solid green, then unbind -- this is where
        // RegenerateMipmapsIfNeededEXT()'s glGenerateMipmap runs (plans/plan_sokol.md SOKOL-39).
        dev.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetRenderTargets({});

        std::vector<Color> level1(16 * 16, Color(0, 0, 0, 0));
        cube.GetData(CubeMapFace::PositiveX, 1, nullptr, level1.data(), 0,
                     static_cast<int>(level1.size()));
        check(AllGreen(level1), "B: level 1 (16x16) reads back solid green -- a GL-incomplete "
                                 "mip chain would read undefined/black instead");

        std::vector<Color> level2(8 * 8, Color(0, 0, 0, 0));
        cube.GetData(CubeMapFace::PositiveX, 2, nullptr, level2.data(), 0,
                     static_cast<int>(level2.size()));
        check(AllGreen(level2), "C: level 2 (8x8) reads back solid green");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    RenderTargetCubeMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    RenderTargetCubeMipTest game;
    game.Run();
    return game.getResult();
}
