// SPDX-License-Identifier: MS-PL
// Task 332: RenderTargetCube constructor/property audit against FNA's RenderTargetCube.cs.
//
// Backend-agnostic (no pixel readback, no rendering) - just constructs RenderTargetCube via
// its public constructor and asserts every property getter against the values FNA
// documents/computes.
//
// Confirmed, NOT-fixed-here gaps are pinned to their CURRENT (buggy) values with an explanatory
// comment rather than silently skipped:
//   - LevelCount is always 1 regardless of `mipMap` (no backend's CreateRenderTargetCube accepts
//     a mip count or allocates RT mip storage) - same shape as Task 331's RenderTarget2D finding,
//     tracked as Task 336 (verify render target mipmap support).
//   - MultiSampleCount is stored verbatim from the constructor argument, never clamped against
//     backend capability (FNA calls FNA3D_GetMaxMultiSampleCount) - same shape as Task 331's
//     RenderTarget2D finding, tracked as Task 337 (verify MSAA render target creation/resolve).
//   - GetTypeName() was missing entirely before this task (inherited TextureCube's "TextureCube"
//     string) - fixed in this task, verified below.
//   - Unlike RenderTarget2D, RenderTargetCube's Dispose(bool) has NO "still bound" guard, and one
//     cannot be added without an architecture change: RenderTargetBinding only stores Texture*,
//     and RenderTargetCube does not inherit Texture (Task 863's architectural gap), so a
//     RenderTargetCube can never be wrapped in a RenderTargetBinding to be compared against.
//     GraphicsDevice::SetRenderTarget(RenderTargetCube*, CubeMapFace) also never records the
//     binding in currentRenderTargets_/GetRenderTargets() for the same reason. Not fixed here -
//     a direct, confirmed downstream consequence of Task 863, not a new independent bug.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class RenderTargetCubePropertiesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // --- Constructor: default preferredMultiSampleCount=0, usage=DiscardContents ---
        {
            RenderTargetCube rt(device, 32, false, SurfaceFormat::Color, DepthFormat::None);
            check(rt.getWidthProperty() == 32, "Width == size (32)");
            check(rt.getHeightProperty() == 32, "Height == size (32)");
            check(rt.getFormatProperty() == SurfaceFormat::Color, "Format == Color");
            check(rt.getDepthStencilFormatProperty() == DepthFormat::None,
                  "DepthStencilFormat == None");
            check(rt.getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents,
                  "RenderTargetUsage defaults to DiscardContents");
            check(rt.getMultiSampleCountProperty() == 0, "MultiSampleCount defaults to 0");
            check(rt.getLevelCountProperty() == 1, "LevelCount == 1 (no mipMap requested)");
            check(rt.getIsContentLostProperty() == false, "IsContentLost == false");
            check(rt.ContentLost.Empty(), "ContentLost has no subscribers by default");
            check(rt.GetTypeName() == "Microsoft.Xna.Framework.Graphics.RenderTargetCube",
                  "GetTypeName() == \"Microsoft.Xna.Framework.Graphics.RenderTargetCube\" (Task 332 fix)");
        }

        // --- Explicit depth format ---
        {
            RenderTargetCube rt(device, 16, false, SurfaceFormat::Color,
                                 DepthFormat::Depth24Stencil8);
            check(rt.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
                  "DepthStencilFormat == Depth24Stencil8");
        }

        // --- Full overload: explicit multiSampleCount + usage ---
        {
            RenderTargetCube rt(device, 8, false, SurfaceFormat::Color, DepthFormat::Depth16,
                                 0, RenderTargetUsage::PreserveContents);
            check(rt.getRenderTargetUsageProperty() == RenderTargetUsage::PreserveContents,
                  "RenderTargetUsage == PreserveContents (explicit)");
            check(rt.getDepthStencilFormatProperty() == DepthFormat::Depth16,
                  "DepthStencilFormat == Depth16");
        }

        // --- Known, tracked gap: mipMap=true does not grow LevelCount (Task 336) ---
        {
            RenderTargetCube rt(device, 64, true, SurfaceFormat::Color, DepthFormat::None);
            // FNA would report LevelCount == 7 for a 64x64 full mip chain (TextureCube's
            // mip-aware backend path is never invoked for render targets). Pinned here rather
            // than silently skipped - tracked as Task 336.
            check(rt.getLevelCountProperty() == 1,
                  "mipMap=true: LevelCount == 1 (known gap, tracked as Task 336)");
        }

        // --- Known, tracked gap: MultiSampleCount is stored verbatim, never clamped (Task 337) ---
        {
            RenderTargetCube rt(device, 8, false, SurfaceFormat::Color, DepthFormat::None,
                                 4, RenderTargetUsage::DiscardContents);
            // No backend's CreateRenderTargetCube accepts/honors a multisample count, so this is
            // pass-through storage only - a confirmed, tracked gap (Task 337).
            check(rt.getMultiSampleCountProperty() == 4,
                  "multiSample=4: MultiSampleCount == 4 (known gap, tracked as Task 337)");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    RenderTargetCubePropertiesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    RenderTargetCubePropertiesTest game;
    game.Run();
    return game.getResult();
}
