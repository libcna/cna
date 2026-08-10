// SPDX-License-Identifier: MS-PL
// Task 207: Tests for GraphicsDevice::Clear overloads.
//
// Verifies:
//   1.  Clear(Color)                     → color changes to the requested value
//   2.  Clear(ClearOptions::Target, …)   → color changes
//   3.  Clear(ClearOptions::DepthBuffer, …) → color does NOT change
//   4.  Clear(Target|DepthBuffer, …)     → color changes, valid depth accepted
//   5.  Clear(Target|DepthBuffer|Stencil, …) → color changes
//   6.  depth < 0.0f with DepthBuffer    → ArgumentOutOfRangeException
//   7.  depth > 1.0f with DepthBuffer    → ArgumentOutOfRangeException
//   8.  depth out-of-range, Target only  → no throw (guard is depth-flag-gated)
//   9.  Clear(Color,float)               → color changes (convenience overload)

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <cstdio>
#include <cmath>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static Color readCenter(GraphicsDevice& dev)
{
    const auto& vp = dev.getViewportProperty();
    const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
    Color px(0, 0, 0, 0);
    dev.GetBackBufferData(&reg, &px, 0, 1);
    return px;
}

// Colour equality within ±2 per channel (GPU rounding).
static bool colorNear(Color a, Color b, int tol = 2)
{
    return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
           std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
           std::abs(a.getBProperty() - b.getBProperty()) <= tol;
}

class ClearOverloadsTest : public Game
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

    template<typename F>
    bool throwsAOOR(F&& fn)
    {
        try { fn(); return false; }
        catch (const System::ArgumentOutOfRangeException&) { return true; }
        catch (...) { return false; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── 1. Clear(Color) ─────────────────────────────────────────────────
        dev.Clear(Color::Red);
        check(colorNear(readCenter(dev), Color::Red),
              "Clear(Color::Red) sets pixel to red");

        // ── 2. Clear(ClearOptions::Target, color, depth, stencil) ───────────
        dev.Clear(ClearOptions::Target, Color::Blue, 1.0f, 0);
        check(colorNear(readCenter(dev), Color::Blue),
              "Clear(Target, Blue, 1.0, 0) sets pixel to blue");

        // ── 3. Clear(DepthBuffer only) — color must NOT change ───────────────
        dev.Clear(Color::Green);                      // baseline: green
        dev.Clear(ClearOptions::DepthBuffer, Color::Red, 1.0f, 0); // depth-only
        check(colorNear(readCenter(dev), Color::Green),
              "Clear(DepthBuffer only) does not change colour");

        // ── 4. Clear(Target|DepthBuffer, color, valid depth, stencil) ────────
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                  Color::White, 0.5f, 0);
        check(colorNear(readCenter(dev), Color::White),
              "Clear(Target|DepthBuffer, White, 0.5, 0) sets pixel to white");

        // ── 5. Clear(Target|DepthBuffer|Stencil, …) ──────────────────────────
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  Color::Black, 1.0f, 0);
        check(colorNear(readCenter(dev), Color::Black),
              "Clear(Target|DepthBuffer|Stencil, Black, 1.0, 0) sets pixel to black");

        // ── 6. depth < 0.0 with DepthBuffer flag → ArgumentOutOfRangeException
        check(throwsAOOR([&]{
                dev.Clear(ClearOptions::DepthBuffer, Color::Red, -0.1f, 0);
              }),
              "Clear with depth=-0.1 and DepthBuffer throws ArgumentOutOfRangeException");

        // ── 7. depth > 1.0 with DepthBuffer flag → ArgumentOutOfRangeException
        check(throwsAOOR([&]{
                dev.Clear(ClearOptions::DepthBuffer, Color::Red, 1.1f, 0);
              }),
              "Clear with depth=1.1 and DepthBuffer throws ArgumentOutOfRangeException");

        // ── 8. depth out-of-range, Target only — guard is depth-flag-gated ───
        //   No DepthBuffer flag → no validation → no throw.
        {
            bool threw = false;
            try { dev.Clear(ClearOptions::Target, Color::Cyan, -99.0f, 0); }
            catch (...) { threw = true; }
            check(!threw, "Clear(Target only, depth=-99) does not throw");
        }

        // ── 9. Clear(Color, float) convenience overload ──────────────────────
        dev.Clear(Color::Magenta, 1.0f);
        check(colorNear(readCenter(dev), Color::Magenta),
              "Clear(Color::Magenta, 1.0) sets pixel to magenta");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    ClearOverloadsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ClearOverloadsTest game;
    game.Run();
    return game.getResult();
}
