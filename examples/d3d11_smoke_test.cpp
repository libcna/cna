// SPDX-License-Identifier: MS-PL
// plan_dx.md Phase DX4/DX80: smoke test for the D3D11 graphics backend's device/swap-chain/
// back-buffer foundation. Real window, real DXGI swap chain, real Clear()+Present()+readback --
// this is the backend's first genuine pixel-correctness proof (DX-28's own bar).
//
// Check A -- real device created with feature level >= 11_0 (design decision 12's floor).
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the EXACT clear color.
// Check C -- a second, different Clear()+readback also matches (not a stale/cached value).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::D3D11;

class D3D11SmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;
    int frame_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        // Give the swap chain one frame to settle before the first real check.
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<D3D11GraphicsBackend&>(dev.GetBackend());

        // Check A: real feature level negotiated, meeting design decision 12's 11_0 floor.
        check(backend.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0,
              "device negotiated feature level 11_0 or higher");
        std::printf("    feature level = 0x%04x, debug layer = %s, tearing = %s\n",
                    static_cast<unsigned>(backend.GetFeatureLevelEXT()),
                    backend.IsDebugLayerEnabledEXT() ? "enabled" : "disabled",
                    backend.IsTearingCapableEXT() ? "capable" : "not capable");

        // Check B: real, correct pixel readback after Clear().
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 4, 4);
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
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color for every pixel");
        }

        // Check C: a second, different Clear() also reads back correctly (not a cached/stale value).
        {
            dev.Clear(Color(200, 100, 50, 255));
            const Microsoft::Xna::Framework::Rectangle region(10, 10, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 200 || p.getGProperty() != 100 || p.getBProperty() != 50 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "a second Clear() at a different region/color also reads back correctly");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 3);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    D3D11SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    D3D11SmokeTest game;
    game.Run();
    return game.getResult();
}
