// SPDX-License-Identifier: MS-PL
// plans/plan_d3d10.md: smoke test for the D3D10 renderer's 3D device state -- a real attached
// DXGI_FORMAT_D24_UNORM_S8_UINT depth-stencil buffer and the ClearColorAndDepth/
// ClearColorDepthAndStencil entry points. Pixel-verified 3D rendering (real shader draws) is
// covered by the shared EasyGL-authored BlendState/DepthStencilState/RasterizerState tests
// (cmake/Tests/DirectX10Tests.cmake). Check D below only confirms the simple state-toggle methods
// don't throw (a smoke-level check, not a pixel proof).
//
// Check A -- renderer.SupportsDepthStencil() reports true (device bring-up succeeded).
// Check B -- GraphicsDevice::Clear(color, depth) (the two-arg overload, which requests
//   Target|DepthBuffer) does not throw and correctly clears the color buffer to the exact
//   requested color, read back via GetBackBufferData() -- proves ClearColorAndDepth ran for real.
// Check C -- ClearColorDepthAndStencil (via GraphicsDevice::Clear(ClearOptions, color, depth,
//   stencil) with all three flags) does not throw and clears color correctly.
// Check D -- SetDepthTestEnabled/SetDepthWriteEnabled do not throw (a smoke-level check).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"

#include "CNA/Internal/Renderers/DirectX10/DirectX10Renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX10;

static constexpr int kCanvasSize = 64;

class D3D10Device3DSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static bool ReadbackMatches(GraphicsDevice& dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const Rectangle region(0, 0, 4, 4);
        std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
        for (const Color& p : pixels)
        {
            if (p.getRProperty() != r || p.getGProperty() != g || p.getBProperty() != b ||
                p.getAProperty() != a)
                return false;
        }
        return true;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX10Renderer&>(dev.GetRenderer());

        // Check A: device bring-up succeeded -- SupportsDepthStencil() reports true.
        check(renderer.SupportsDepthStencil(), "SupportsDepthStencil() reports true after device bring-up");

        // Check B: Clear(color, depth) -- the two-arg overload requesting Target|DepthBuffer --
        // does not throw and correctly clears the color buffer to the exact requested color.
        {
            bool threw = false;
            try { dev.Clear(Color(30, 60, 90, 255), 0.5f); }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("Clear(color, depth) threw: %s\n", e.what());
            }
            check(!threw && ReadbackMatches(dev, 30, 60, 90, 255),
                  "Clear(color, depth) does not throw and clears color correctly (real ClearColorAndDepth)");
        }

        // Check C: ClearColorDepthAndStencil path (all three flags) does not throw and clears
        // color correctly -- D3D10's stencil clear is real (a direct ClearDepthStencilView call).
        {
            bool threw = false;
            try
            {
                dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                          Color(11, 22, 33, 255), 0.25f, 7);
            }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("Clear(Target|DepthBuffer|Stencil) threw: %s\n", e.what());
            }
            check(!threw && ReadbackMatches(dev, 11, 22, 33, 255),
                  "Clear(Target|DepthBuffer|Stencil) does not throw and clears color correctly");
        }

        // Check D: state-toggle methods don't throw (smoke-level only -- these are no-ops in this
        // v1, since real depth-test/blend state comes entirely from ApplyDepthStencilState's own
        // state objects, not a separate enable/disable toggle).
        {
            bool threw = false;
            try
            {
                renderer.SetDepthTestEnabled(true);
                renderer.SetDepthWriteEnabled(true);
            }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("SetDepthTestEnabled/SetDepthWriteEnabled threw: %s\n", e.what());
            }
            check(!threw, "SetDepthTestEnabled/SetDepthWriteEnabled do not throw");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 4);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    D3D10Device3DSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    D3D10Device3DSmokeTest game;
    game.Run();
    return game.getResult();
}
