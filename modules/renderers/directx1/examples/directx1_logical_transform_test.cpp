// SPDX-License-Identifier: MS-PL
// plans/plan_dx1.md Phase O7 (DX1-68): TransformWindowToLogical/TransformLogicalToWindow (real
// letterbox scale+offset) tests for the DIRECTX1 (real DirectDraw v1, run under Wine -- no
// ../free-direct anywhere in this renderer) graphics renderer.
//
// The game requests a 64x64 logical/virtual resolution. Rather than forcing a specific physical
// window size (SDL_SetWindowSize is not reliably honored by every window manager/virtual
// display -- confirmed empirically in this dev environment, where the window snaps to the full
// display size regardless of what's requested), this test queries whatever the REAL physical
// window size actually is and verifies the letterbox math against invariant PROPERTIES of
// "uniform scale to fit, centered" (ComputeLetterbox, the same helper Present() itself uses --
// design decision 4), rather than a single hardcoded expected pixel value. This makes the test
// meaningful and portable across environments, including ones where the physical and logical
// sizes coincidentally match (scale=1, offset=0 -- the invariants still hold exactly).
//
// Check A -- round trip: TransformLogicalToWindow() then TransformWindowToLogical() returns the
//   original point, for several sample logical points including the corners and center.
// Check B -- centering: the logical center (32,32) maps to the window's real geometric center
//   (physW/2, physH/2) -- the defining property of a *centered* letterbox.
// Check C -- uniform scale: the window-space distance for a horizontal logical step equals the
//   window-space distance for the same-magnitude vertical logical step (no anisotropic stretch).
// Check D -- "fit largest without overflow": the scale found in Check C equals
//   min(physW/64, physH/64), the real letterbox scale formula, not just physW/64 or physH/64
//   alone (either of which would overflow on a non-matching aspect ratio).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/DirectX1/DirectX1Renderer.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX1;

static constexpr int kLogicalSize = 64;

static bool NearlyEqual(float a, float b, float tol = 0.05f)
{
    return std::fabs(a - b) <= tol;
}

class DirectX1LogicalTransformTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 4;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX1Renderer&>(dev.GetRenderer());

        SDL_Window* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window, &physW, &physH);
        std::printf("Real physical window size: %dx%d (logical: %dx%d)\n", physW, physH, kLogicalSize, kLogicalSize);

        // Check A: round trip for several sample logical points.
        {
            const float samples[][2] = { {0.0f, 0.0f}, {64.0f, 0.0f}, {0.0f, 64.0f}, {64.0f, 64.0f}, {32.0f, 32.0f} };
            bool allOk = true;
            for (const auto& s : samples)
            {
                float wx = 0.0f, wy = 0.0f;
                if (!renderer.TransformLogicalToWindow(s[0], s[1], wx, wy)) { allOk = false; break; }
                float logX = 0.0f, logY = 0.0f;
                if (!renderer.TransformWindowToLogical(wx, wy, logX, logY)) { allOk = false; break; }
                if (!NearlyEqual(logX, s[0]) || !NearlyEqual(logY, s[1])) { allOk = false; break; }
            }
            check(allOk, "logical -> window -> logical round trip returns the original point (DX1-68)");
        }

        // Check B: the logical center maps to the window's real geometric center (centered
        // letterbox property).
        {
            float wx = 0.0f, wy = 0.0f;
            const bool ok1 = renderer.TransformLogicalToWindow(32.0f, 32.0f, wx, wy);
            check(ok1 && NearlyEqual(wx, static_cast<float>(physW) / 2.0f) &&
                  NearlyEqual(wy, static_cast<float>(physH) / 2.0f),
                  "logical center (32,32) maps to the window's real geometric center (DX1-68)");
        }

        // Check C: uniform scale -- a horizontal and a vertical logical step of the same
        // magnitude produce window-space steps of the same length (no anisotropic stretch).
        float measuredScale = 0.0f;
        {
            float ox = 0.0f, oy = 0.0f, hx = 0.0f, hy = 0.0f, vx = 0.0f, vy = 0.0f;
            renderer.TransformLogicalToWindow(0.0f, 0.0f, ox, oy);
            renderer.TransformLogicalToWindow(10.0f, 0.0f, hx, hy);
            renderer.TransformLogicalToWindow(0.0f, 10.0f, vx, vy);
            const float hLen = std::sqrt((hx - ox) * (hx - ox) + (hy - oy) * (hy - oy));
            const float vLen = std::sqrt((vx - ox) * (vx - ox) + (vy - oy) * (vy - oy));
            measuredScale = hLen / 10.0f;
            check(NearlyEqual(hLen, vLen, 0.1f),
                  "a horizontal and vertical logical step produce equal-length window-space steps (DX1-68, no stretch)");
        }

        // Check D: the measured scale is exactly min(physW/64, physH/64) -- "fit largest without
        // overflow", not just one axis alone.
        {
            const float expectedScale = std::min(static_cast<float>(physW) / kLogicalSize,
                                                 static_cast<float>(physH) / kLogicalSize);
            check(NearlyEqual(measuredScale, expectedScale, 0.1f),
                  "the real scale is min(physW/64, physH/64), the actual letterbox formula (DX1-68)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    DirectX1LogicalTransformTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kLogicalSize);
        gdm_->setPreferredBackBufferHeightProperty(kLogicalSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX1LogicalTransformTest game;
    game.Run();
    return game.getResult();
}
