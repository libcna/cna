// SPDX-License-Identifier: MS-PL
// plan_freedirect.md Phase X1/X2 (DX3-1..DX3-18): smoke test for the DIRECTX3 (DirectDraw, via the
// ../free-direct sibling) graphics renderer's foundation -- real DirectDrawCreate/
// SetCooperativeLevel/SetDisplayMode/CreateSurface device bring-up, real Clear()/Present(), real
// pixel readback. SpriteBatch/Texture2D draws are not yet implemented (Phase X3/X4).
//
// Check A -- GameWindow handle returns a real, non-null window (unlike HEADLESS/SOFTWARE, DIRECTX3
//   genuinely needs one -- free-direct's SetCooperativeLevel wraps it via reinterpret_cast).
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the exact clear color
//   (RGB and alpha), read from DIRECTX3's own Lockable shadow-backbuffer surface (design decision 5's
//   fix for free-direct's IDirectDrawSurface::Lock() never exposing a writable pointer for the
//   *primary* surface).
// Check D -- Clear() honors a non-opaque requested alpha (128) exactly, not silently forced to
//   255 -- a real bug found in review: free-direct's own FillColor() (the DDBLT_COLORFILL path)
//   hardcodes the written alpha byte to 255 unconditionally, so Clear() now writes all 4 channels
//   directly via Lock()/Unlock() instead.
// Check C -- Present() (the shadow-backbuffer -> primary identity Blt()) does not throw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/FreeDirect/FreeDirectRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::FreeDirect;

static constexpr int kCanvasSize = 64;

class FreeDirectSmokeTest : public Game
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
        auto& renderer = static_cast<FreeDirectRenderer&>(dev.GetRenderer());

        // Check A: real window.
        check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GraphicsDevice has a real window under the DIRECTX3 renderer");

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

        // Check D: Clear() honors a non-opaque requested alpha exactly. Real bug found and fixed
        // in review: Clear() originally used DDBLT_COLORFILL, but free-direct's own FillColor()
        // hardcodes the written alpha byte to 255 unconditionally, so any requested alpha other
        // than 255 was silently discarded. Clear() now writes all 4 channels directly via
        // Lock()/Unlock() instead.
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
    FreeDirectSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    FreeDirectSmokeTest game;
    game.Run();
    return game.getResult();
}
