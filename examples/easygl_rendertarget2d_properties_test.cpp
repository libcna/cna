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
// truly mip-complete right now; Vulkan/Bgfx accept and ignore the `mipMap` flag (Task 877).
//
// One remaining confirmed, NOT-fixed-here gap is pinned to its CURRENT (buggy) value with an
// explanatory comment rather than silently skipped, so Task 337 has a regression marker to update:
//   - MultiSampleCount is stored verbatim from the constructor argument, never clamped against
//     backend capability and never wired into actual multisampled RT creation (no backend's
//     CreateRenderTarget2D even accepts a multisample count) - tracked as Task 337 (verify MSAA
//     render target creation/resolve).

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

        // --- Known, tracked gap: MultiSampleCount is stored verbatim, never clamped (Task 337) ---
        {
            RenderTarget2D rt(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None,
                               4, RenderTargetUsage::DiscardContents);
            // No backend's CreateRenderTarget2D accepts/honors a multisample count, so this is
            // pass-through storage only - a confirmed, tracked gap (Task 337).
            check(rt.getMultiSampleCountProperty() == 4,
                  "Ctor3 multiSample=4: MultiSampleCount == 4 (known gap, tracked as Task 337)");
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
