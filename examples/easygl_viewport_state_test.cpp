// SPDX-License-Identifier: MS-PL
// Task 208: Verify GraphicsDevice viewport state.
//
// Checks:
//   1.  Initial viewport covers the full backbuffer (matches PresentationParameters).
//   2.  setViewportProperty() round-trips — getViewportProperty() returns exactly
//       what was set (x, y, width, height, minDepth, maxDepth).
//   3.  Viewport survives a Clear() call without being reset.
//   4.  Viewport survives DrawUserPrimitives() without being reset.
//   5.  A second explicit setViewportProperty() updates the stored value.
//   6.  Restoring the original viewport via setViewportProperty() works.
//   7.  minDepth/maxDepth survive across set/get.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cstdio>
#include <cmath>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool vpEq(const Viewport& a, const Viewport& b)
{
    return a.getXProperty() == b.getXProperty() &&
           a.getYProperty() == b.getYProperty() &&
           a.getWidthProperty()  == b.getWidthProperty() &&
           a.getHeightProperty() == b.getHeightProperty() &&
           std::abs(a.getMinDepthProperty() - b.getMinDepthProperty()) < 1e-5f &&
           std::abs(a.getMaxDepthProperty() - b.getMaxDepthProperty()) < 1e-5f;
}

class ViewportStateTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        // ── 1. Initial viewport covers full backbuffer ────────────────────
        const auto& pp = device.getPresentationParametersProperty();
        const Viewport initial = device.getViewportProperty();
        check(initial.getWidthProperty()  == pp.getBackBufferWidthProperty() &&
              initial.getHeightProperty() == pp.getBackBufferHeightProperty(),
              "Initial viewport matches PresentationParameters backbuffer size");
        check(initial.getXProperty() == 0 && initial.getYProperty() == 0,
              "Initial viewport origin is (0,0)");

        // ── 2. setViewportProperty round-trip ─────────────────────────────
        Viewport custom(50, 30, 320, 240);
        custom.setMinDepthProperty(0.1f);
        custom.setMaxDepthProperty(0.9f);
        device.setViewportProperty(custom);
        check(vpEq(device.getViewportProperty(), custom),
              "setViewportProperty()/getViewportProperty() round-trip");

        // ── 3. Viewport survives Clear() ──────────────────────────────────
        device.Clear(Color::CornflowerBlue);
        check(vpEq(device.getViewportProperty(), custom),
              "Viewport unchanged after Clear(Color)");

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Black, 1.0f, 0);
        check(vpEq(device.getViewportProperty(), custom),
              "Viewport unchanged after Clear(ClearOptions,Color,depth,stencil)");

        // ── 4. Viewport survives DrawUserPrimitives() ─────────────────────
        BasicEffect fx(device);
        fx.Apply();
        VertexPositionColor tri[3]{};
        try { device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1); }
        catch (...) {} // GPU errors are fine — we only care about viewport stability
        check(vpEq(device.getViewportProperty(), custom),
              "Viewport unchanged after DrawUserPrimitives()");

        // ── 5. Second explicit set updates the value ──────────────────────
        Viewport second(10, 20, 100, 80);
        device.setViewportProperty(second);
        check(vpEq(device.getViewportProperty(), second),
              "Second setViewportProperty() updates to new value");
        check(!vpEq(device.getViewportProperty(), custom),
              "After second set, viewport is no longer the first custom value");

        // ── 6. Restore original viewport ──────────────────────────────────
        device.setViewportProperty(initial);
        check(vpEq(device.getViewportProperty(), initial),
              "Viewport restored to initial value via setViewportProperty()");

        // ── 7. minDepth / maxDepth survive set/get ────────────────────────
        Viewport depthVp(0, 0, 200, 150);
        depthVp.setMinDepthProperty(0.25f);
        depthVp.setMaxDepthProperty(0.75f);
        device.setViewportProperty(depthVp);
        const Viewport got = device.getViewportProperty();
        check(std::abs(got.getMinDepthProperty() - 0.25f) < 1e-5f &&
              std::abs(got.getMaxDepthProperty() - 0.75f) < 1e-5f,
              "minDepth=0.25 / maxDepth=0.75 survive setViewportProperty/getViewportProperty");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    ViewportStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ViewportStateTest game;
    game.Run();
    return game.getResult();
}
