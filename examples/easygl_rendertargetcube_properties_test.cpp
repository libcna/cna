// SPDX-License-Identifier: MS-PL
// Task 332: RenderTargetCube constructor/property audit against FNA's RenderTargetCube.cs.
//
// Backend-agnostic (no pixel readback, no rendering) - just constructs RenderTargetCube via
// its public constructor and asserts every property getter against the values FNA
// documents/computes.
//
// Task 336 fix: LevelCount now correctly reflects `mipMap`, and EasyGL actually allocates +
// auto-generates the full mip chain (per-face) on unbind. Holds on Vulkan/Bgfx too (shared,
// backend-agnostic computation) but only EasyGL's GPU resource is truly mip-complete right now
// (Task 878).
//
// Task 337 fix: MultiSampleCount now reflects the backend's real, device-clamped value (mirroring
// FNA's MathHelper.ClosestMSAAPower + FNA3D_GetMaxMultiSampleCount), and EasyGL actually creates a
// shared multisampled color renderbuffer (reused across faces) resolved into the correct face's
// texture image on unbind. Same Vulkan/Bgfx caveat as LevelCount above (Task 879).
//
// One remaining note:
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

        // --- Task 336 fix: mipMap=true correctly grows LevelCount ---
        {
            RenderTargetCube rt(device, 64, true, SurfaceFormat::Color, DepthFormat::None);
            // Matches FNA: LevelCount == 7 for a 64x64 full mip chain.
            check(rt.getLevelCountProperty() == 7,
                  "mipMap=true: LevelCount == 7 (64x64 full mip chain, Task 336 fix)");
        }

        // --- Task 337 fix: MultiSampleCount reflects the real, per-backend value, never a
        // blind pass-through (see easygl_rendertarget2d_properties_test.cpp for the full
        // rationale — this test is shared between EasyGL and Vulkan builds). ---
        {
            RenderTargetCube rt(device, 8, false, SurfaceFormat::Color, DepthFormat::None,
                                 4, RenderTargetUsage::DiscardContents);
            const int v = rt.getMultiSampleCountProperty();
            check(v == 4 || v == 0,
                  "multiSample=4: MultiSampleCount == 4 (EasyGL, real) or 0 (Vulkan/Bgfx, not yet implemented)");
        }
        {
            RenderTargetCube rt(device, 8, false, SurfaceFormat::Color, DepthFormat::None,
                                 9999, RenderTargetUsage::DiscardContents);
            const int v = rt.getMultiSampleCountProperty();
            const bool valid = (v == 0) || (v > 0 && v < 9999 && (v & (v - 1)) == 0);
            check(valid,
                  "multiSample=9999 (over any real cap): never a blind pass-through, Task 337 fix");
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
