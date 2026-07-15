// SPDX-License-Identifier: MS-PL
// plan_dx3.md Phase X1/X2 (DX3-1..DX3-18): smoke test for the DX3 (DirectDraw, via the
// ../free-direct sibling) graphics backend's foundation -- real DirectDrawCreate/
// SetCooperativeLevel/SetDisplayMode/CreateSurface device bring-up, real Clear()/Present(), real
// pixel readback. SpriteBatch/Texture2D draws are not yet implemented (Phase X3/X4).
//
// Check A -- GetWindowInternal() returns a real, non-null window (unlike HEADLESS/SOFTWARE, DX3
//   genuinely needs one -- free-direct's SetCooperativeLevel wraps it via reinterpret_cast).
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the exact clear color, read
//   from DX3's own Lockable shadow-backbuffer surface (design decision 5's fix for free-direct's
//   IDirectDrawSurface::Lock() never exposing a writable pointer for the *primary* surface).
// Check C -- Present() (the shadow-backbuffer -> primary identity Blt()) does not throw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Dx3;

static constexpr int kCanvasSize = 64;

class Dx3SmokeTest : public Game
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
        auto& backend = static_cast<Dx3GraphicsBackend&>(dev.GetBackend());

        // Check A: real window.
        check(backend.GetWindowInternal() != nullptr, "GraphicsDevice has a real window under the DX3 backend");

        // Check B: real, correct pixel readback after Clear(), via the shadow-backbuffer surface.
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 20 || p.getGProperty() != 40 || p.getBProperty() != 60)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color for every pixel");
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

        std::printf("=== %d/%d PASS ===\n", passCount_, 3);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    Dx3SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx3SmokeTest game;
    game.Run();
    return game.getResult();
}
