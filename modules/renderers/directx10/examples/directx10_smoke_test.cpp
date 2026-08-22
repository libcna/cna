// SPDX-License-Identifier: MS-PL
// plans/plan_d3d10.md: smoke test for the D3D10 (real Direct3D 10 via DXVK's d3d10core, real HLSL
// shaders) graphics renderer's device/swap-chain/back-buffer foundation.
//
// Check A -- GameWindow handle returns a real, non-null window.
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the exact clear color
//   (RGB and alpha), read via a staging-texture Map (D3D10-0f, spike-confirmed).
// Check C -- Clear() honors a non-opaque requested alpha (128) exactly.
// Check D -- Present() does not throw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/DirectX10/DirectX10Renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX10;

static constexpr int kCanvasSize = 64;

class D3D10SmokeTest : public Game
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
        auto& renderer = static_cast<DirectX10Renderer&>(dev.GetRenderer());

        check(getWindowProperty().getHandleProperty() != 0,
              "GraphicsDevice has a real window under the D3D10 renderer");

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
    D3D10SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    D3D10SmokeTest game;
    game.Run();
    return game.getResult();
}
