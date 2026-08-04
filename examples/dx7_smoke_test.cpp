// SPDX-License-Identifier: MS-PL
// plan_dx30.md Phase P2 (DX30-10, ported from plan_dx2.md's DX2-10/DX1-10..DX1-18): smoke test for
// the DX7 (real DirectDraw v4, run under Wine -- no ../free-direct anywhere in this backend)
// graphics backend's foundation -- real DirectDrawCreate -> QueryInterface(IID_IDirectDraw2) ->
// SetCooperativeLevel(DDSCL_NORMAL) -> CreateSurface device bring-up, real Clear()/Present(), real
// pixel readback. SpriteBatch/Texture2D draws are covered by dx30_spritebatch_test.cpp (Phase P4).
//
// This test's own success IS the proof the IDirectDraw2 upgrade (plan_dx30.md design decision 2)
// actually happened: Dx7GraphicsBackend's constructor unconditionally throws if
// QueryInterface(IID_IDirectDraw2) fails, so every check below only ever runs against a genuine
// v2 object -- no separate "did the upgrade happen" CTest is needed on top of that.
//
// Check A -- GetWindowInternal() returns a real, non-null window (DX7 needs a genuine Win32 HWND,
//   obtained from it via SDL_PROP_WINDOW_WIN32_HWND_POINTER, design decision 3).
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the exact clear color
//   (RGB and alpha), read from DX7's own Lockable shadow-backbuffer surface (design decision 4).
// Check D -- Clear() honors a non-opaque requested alpha (128) exactly, not silently forced to
//   255 -- written directly via Lock()/Unlock() (FillSurfaceColor), not DDBLT_COLORFILL, so this
//   never depends on how a given ddraw.dll's ColorFill happens to treat the alpha channel.
// Check C -- Present() (the shadow-backbuffer -> primary letterboxed Blt()) does not throw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/Dx7/Dx7GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Dx7;

static constexpr int kCanvasSize = 64;

class Dx7SmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
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
        auto& backend = static_cast<Dx7GraphicsBackend&>(dev.GetBackend());

        // Check A: real window.
        check(backend.GetWindowInternal() != nullptr, "GraphicsDevice has a real window under the DX7 backend");

        // Check B: real, correct pixel readback after Clear(), via the shadow-backbuffer surface,
        // including the alpha channel.
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 20 || p.getGProperty() != 40 || p.getBProperty() != 60 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color (incl. alpha) for every pixel");
        }

        // Check D: Clear() honors a non-opaque requested alpha exactly. Uses FillSurfaceColor
        // (direct Lock()/Unlock() writes of all 4 channels), not DDBLT_COLORFILL, proactively
        // avoiding the class of bug DX3 found and fixed for its own DDBLT_COLORFILL-based Clear()
        // (plan_freedirect.md DX3-14, free-direct's FillColor() hardcoding alpha to 255).
        {
            dev.Clear(Color(10, 20, 30, 128));
            const Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 10 || p.getGProperty() != 20 || p.getBProperty() != 30 ||
                    p.getAProperty() != 128)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "Clear() honors a non-opaque requested alpha exactly (128), not forced to 255");
        }

        // Check C: Present() (shadow backbuffer -> primary identity Blt()) does not throw.
        {
            bool threw = false;
            try { dev.Present(); }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("Present() threw: %s\n", e.what());
            }
            check(!threw, "Present() does not throw");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 4);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    Dx7SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx7SmokeTest game;
    game.Run();
    return game.getResult();
}
