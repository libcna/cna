// SPDX-License-Identifier: MS-PL
// Task 331: RenderTarget2D constructor/property audit against FNA's RenderTarget2D.cs.
//
// Backend-agnostic (no pixel readback, no rendering) - just constructs RenderTarget2D via
// each public constructor overload and asserts every property getter against the values FNA
// documents/computes, plus the newly-added IsContentLost/ContentLost (Task 331 found these were
// missing entirely - RenderTargetCube already had them, RenderTarget2D did not).
//
// Task 336 fix: LevelCount now correctly reflects `mipMap` (mirroring Texture2D/TextureCube's own
// CalculateMipLevels), and EasyGL actually allocates + auto-generates the full mip chain on
// unbind (mirroring FNA3D's OPENGL_ResolveTarget). This assertion holds on Vulkan/Bgfx too — the
// LevelCount computation is shared, backend-agnostic C++ — but only EasyGL's GPU resource is
// truly mip-complete right now; Vulkan/Bgfx accept and ignore the `mipMap` flag (Task 878).
//
// Task 337 fix: MultiSampleCount now reflects the backend's real, device-clamped value (mirroring
// FNA's MathHelper.ClosestMSAAPower + FNA3D_GetMaxMultiSampleCount), not a raw pass-through, and
// EasyGL actually creates a multisampled color/depth renderbuffer, resolved into the sampleable
// texture on unbind. Same Vulkan/Bgfx caveat as LevelCount above (Task 879).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class RenderTarget2DPropertiesTest : public Game
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

        // --- Constructor 1: RenderTarget2D(device, width, height) ---
        // FNA: mipMap=false, preferredFormat=Color, preferredDepthFormat=None,
        //      preferredMultiSampleCount=0, usage=DiscardContents.
        {
            RenderTarget2D rt(device, 64, 32);
            check(rt.getWidthProperty() == 64, "Ctor1: Width == 64");
            check(rt.getHeightProperty() == 32, "Ctor1: Height == 32");
            check(rt.getFormatProperty() == SurfaceFormat::Color, "Ctor1: Format == Color");
            check(rt.getDepthStencilFormatProperty() == DepthFormat::None,
                  "Ctor1: DepthStencilFormat == None");
            check(rt.getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents,
                  "Ctor1: RenderTargetUsage == DiscardContents");
            check(rt.getLevelCountProperty() == 1, "Ctor1: LevelCount == 1 (no mipMap requested)");
            check(rt.getIsContentLostProperty() == false, "Ctor1: IsContentLost == false");
            check(rt.ContentLost.Empty(), "Ctor1: ContentLost has no subscribers by default");
        }

        // --- Constructor 2: RenderTarget2D(device, w, h, mipMap, format, depthFormat) ---
        {
            RenderTarget2D rt(device, 16, 16, false, SurfaceFormat::Color,
                               DepthFormat::Depth24Stencil8);
            check(rt.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
                  "Ctor2: DepthStencilFormat == Depth24Stencil8");
            check(rt.getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents,
                  "Ctor2: RenderTargetUsage defaults to DiscardContents");
            check(rt.getMultiSampleCountProperty() == 0,
                  "Ctor2: MultiSampleCount defaults to 0");
        }

        // --- Constructor 3: full overload (mipMap, format, depthFormat, multiSample, usage) ---
        {
            RenderTarget2D rt(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth16,
                               0, RenderTargetUsage::PreserveContents);
            check(rt.getRenderTargetUsageProperty() == RenderTargetUsage::PreserveContents,
                  "Ctor3: RenderTargetUsage == PreserveContents (explicit)");
            check(rt.getDepthStencilFormatProperty() == DepthFormat::Depth16,
                  "Ctor3: DepthStencilFormat == Depth16");
        }

        // --- Task 336 fix: mipMap=true correctly grows LevelCount ---
        {
            RenderTarget2D rt(device, 64, 64, true, SurfaceFormat::Color, DepthFormat::None);
            // Matches FNA: LevelCount == 7 for a 64x64 full mip chain.
            check(rt.getLevelCountProperty() == 7,
                  "Ctor2 mipMap=true: LevelCount == 7 (64x64 full mip chain, Task 336 fix)");
        }

        // --- Task 337 fix: MultiSampleCount reflects the real, per-backend value, never a
        // blind pass-through. EasyGL actually implements MSAA-for-RT (clamped to real device
        // capability); Vulkan/Bgfx don't yet (Task 879) and honestly report 0 rather than
        // lying about support they don't have — this test is shared between both backends, so
        // both outcomes are accepted as correct; only an unclamped pass-through (e.g. exactly
        // 9999) would be a failure. ---
        {
            RenderTarget2D rt(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None,
                               4, RenderTargetUsage::DiscardContents);
            const int v = rt.getMultiSampleCountProperty();
            check(v == 4 || v == 0,
                  "Ctor3 multiSample=4: MultiSampleCount == 4 (EasyGL, real) or 0 (Vulkan/Bgfx, not yet implemented)");
        }
        {
            RenderTarget2D rt(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None,
                               9999, RenderTargetUsage::DiscardContents);
            // 9999 is far beyond any real GPU's GL_MAX_SAMPLES - must never come back unchanged
            // (that would mean a blind pass-through, the pre-Task-337 bug); must be 0 (Vulkan/
            // Bgfx) or a real, achievable, clamped power-of-two value (EasyGL).
            const int v = rt.getMultiSampleCountProperty();
            const bool valid = (v == 0) || (v > 0 && v < 9999 && (v & (v - 1)) == 0);
            check(valid,
                  "Ctor3 multiSample=9999 (over any real cap): never a blind pass-through, Task 337 fix");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    RenderTarget2DPropertiesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    RenderTarget2DPropertiesTest game;
    game.Run();
    return game.getResult();
}
