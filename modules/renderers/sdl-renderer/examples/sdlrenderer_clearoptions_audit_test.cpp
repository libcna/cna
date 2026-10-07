// SPDX-License-Identifier: MS-PL
// Task 716: Verify GraphicsDevice::Clear (all ClearOptions combinations) on SDL_Renderer.
// Start of the GraphicsDevice-lifecycle section (716-719).
//
// ClearOptions is a 3-bit flags enum: Target=1, DepthBuffer=2, Stencil=4 (8 combinations,
// including "none"). SOFTWARE-333 restored Microsoft XNA's validation: a Clear that names a depth
// or stencil plane the active surface does not have throws InvalidOperationException before any
// renderer dispatch, so the refusal is atomic -- not even the Target half of the request clears.
// (This file originally pinned the pre-Task-871 behaviour, where a Stencil request was silently
// ignored and Target|Stencil cleared the colour target; that expectation is superseded.)
// THIS 2D-only renderer's back buffer has neither a depth nor a stencil plane, so every
// combination naming DepthBuffer or Stencil is refused that way and leaves the colour target
// untouched; the only non-throwing combinations are Target alone and the empty/"none" case (a
// no-op, correctly so -- XNA itself defines no-op semantics for an empty ClearOptions).
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <memory>
#include <typeinfo>

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

    // True iff Clear threw exactly System::InvalidOperationException (SOFTWARE-333's missing-plane
    // refusal); any other exception escapes and fails the run.
    bool ClearThrows(GraphicsDevice& dev, ClearOptions options)
    {
        try
        {
            dev.Clear(options, Color(255, 0, 255, 255), 1.0f, 0);
            return false;
        }
        catch (const System::InvalidOperationException& e)
        {
            return typeid(e) == typeid(System::InvalidOperationException);
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

        // --- Stencil alone: refused (this renderer has no stencil buffer), and the refusal leaves
        // the color target untouched. ---
        dev.Clear(Color(0, 255, 0, 255));
        check(ClearThrows(dev, ClearOptions::Stencil), "ClearOptions::Stencil alone throws (no stencil buffer on this renderer)");
        Color afterStencilOnly = SampleCenter(dev);
        check(afterStencilOnly.getRProperty() <= 15 && afterStencilOnly.getGProperty() >= 240 && afterStencilOnly.getBProperty() <= 15,
              "ClearOptions::Stencil alone stores nothing -- the prior Green fill survives untouched");

        // --- Target|Stencil: refused atomically -- the Target half does not clear either. ---
        check(ClearThrows(dev, ClearOptions::Target | ClearOptions::Stencil), "ClearOptions::Target|Stencil throws (no stencil buffer on this renderer)");
        Color afterTargetStencil = SampleCenter(dev);
        check(afterTargetStencil.getRProperty() <= 15 && afterTargetStencil.getGProperty() >= 240 && afterTargetStencil.getBProperty() <= 15,
              "ClearOptions::Target|Stencil is refused atomically -- the color target keeps the prior Green fill");

        Exit();
    }

public:
    SdlClearOptionsAuditTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // SOFTWARE-213 made GetBackBufferData HiDef-only, as in XNA 4.0, and this fixture reads the
        // back buffer -- so under GraphicsDeviceManager's default Reach profile it aborted before
        // its first check (plans/plan_gpu_test_isolation.md GTI-0007 names the class).
        gdm_->setGraphicsProfileProperty(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
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
