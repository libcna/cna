// SPDX-License-Identifier: MS-PL
// Task 716: Verify GraphicsDevice::Clear (all ClearOptions combinations) on SDL_Renderer.
// Start of the GraphicsDevice-lifecycle section (716-719).
//
// ClearOptions is a 3-bit flags enum: Target=1, DepthBuffer=2, Stencil=4 (8 combinations,
// including "none"). GraphicsDevice::Clear(ClearOptions, ...) only actually branches on
// Target/DepthBuffer -- its `stencil` parameter is explicitly discarded (`(void)stencil;`), an
// ALREADY-TRACKED, cross-renderer, not-yet-fixed gap (NEXT.md §5, Task 871) -- NOT something this
// task fixes; it is verified/pixel-tested here in its concrete SDL_Renderer manifestation only.
// On THIS 2D-only renderer, ANY combination including DepthBuffer routes to ClearDepth or
// ClearColorAndDepth, both unconditional ThrowNo3D -- so the only non-throwing combinations here
// are Target alone, Stencil alone (a genuine no-op per the Task 871 gap), Target|Stencil (Target
// clears normally, Stencil silently ignored), and the empty/"none" case (also a no-op, correctly
// so -- XNA itself defines no-op semantics for an empty ClearOptions).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlClearOptionsAuditTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

    bool ClearThrows(GraphicsDevice& dev, ClearOptions options)
    {
        try
        {
            dev.Clear(options, Color(255, 0, 255, 255), 1.0f, 0);
            return false;
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
    }

    Color SampleCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        Color pixel(0, 0, 0, 0);
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // --- Target alone: clears the color target, no throw. ---
        dev.Clear(Color(0, 0, 0, 255));
        check(!ClearThrows(dev, ClearOptions::Target), "ClearOptions::Target alone does not throw");
        Color afterTarget = SampleCenter(dev);
        check(afterTarget.getRProperty() >= 240 && afterTarget.getBProperty() >= 240,
              "ClearOptions::Target alone genuinely clears the color target to the requested colour");

        // --- Any combination including DepthBuffer throws (this 2D-only renderer has no depth buffer). ---
        check(ClearThrows(dev, ClearOptions::DepthBuffer), "ClearOptions::DepthBuffer alone throws (no depth buffer on this renderer)");
        check(ClearThrows(dev, ClearOptions::Target | ClearOptions::DepthBuffer), "ClearOptions::Target|DepthBuffer throws");
        check(ClearThrows(dev, ClearOptions::DepthBuffer | ClearOptions::Stencil), "ClearOptions::DepthBuffer|Stencil throws");
        check(ClearThrows(dev, ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil), "ClearOptions::Target|DepthBuffer|Stencil throws");

        // --- Stencil alone: a genuine no-op (Task 871's already-tracked gap) -- does not throw,
        // does not clear the color target either. ---
        dev.Clear(Color(0, 255, 0, 255));
        check(!ClearThrows(dev, ClearOptions::Stencil), "ClearOptions::Stencil alone does not throw");
        Color afterStencilOnly = SampleCenter(dev);
        check(afterStencilOnly.getRProperty() <= 15 && afterStencilOnly.getGProperty() >= 240 && afterStencilOnly.getBProperty() <= 15,
              "ClearOptions::Stencil alone is a genuine no-op -- the prior Green fill survives untouched");

        // --- Target|Stencil: Target clears normally, Stencil silently ignored, no throw. ---
        check(!ClearThrows(dev, ClearOptions::Target | ClearOptions::Stencil), "ClearOptions::Target|Stencil does not throw");
        Color afterTargetStencil = SampleCenter(dev);
        check(afterTargetStencil.getRProperty() >= 240 && afterTargetStencil.getBProperty() >= 240,
              "ClearOptions::Target|Stencil clears the color target (Stencil silently ignored)");

        Exit();
    }

public:
    SdlClearOptionsAuditTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlClearOptionsAuditTest game;
    game.Run();
    return game.getResult();
}
