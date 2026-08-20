// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O3 (DX2-20..DX2-26): smoke test for DIRECTX5's real Direct3D v3 device bring-up (a port of DIRECTX3's own, itself a port of DIRECTX2's) --
// IDirect3D2/IDirect3DDevice2/IDirect3DViewport2 creation against the shadow-backbuffer surface,
// a real attached 16-bit Z-buffer, and the newly-real ClearColorAndDepth/ClearDepth/etc entry
// points. VertexBuffer/IndexBuffer storage (Phase O5), the 3D draw path itself
// (DrawColoredPrimitives/DrawPrimitivesEx, Phase O4), and state application (SetDepthTestEnabled/
// ApplyRasterizerState/etc, Phase O6) are all real now -- pixel-verified 3D rendering is covered
// by directx2_colored_primitives_test.cpp/directx2_ztest_test.cpp/etc, not this file. Check D below only
// confirms the simple state-toggle methods don't throw anymore (a smoke-level check) -- DirectX5_ZTest
// already covers real depth-test behavior in depth.
//
// Check A -- renderer.SupportsDepthStencil() reports true (device bring-up succeeded; DIRECTX1 always
//   reports false here).
// Check B -- GraphicsDevice::Clear(color, depth) (the two-arg overload, which requests
//   Target|DepthBuffer) does not throw and correctly clears the color buffer to the exact
//   requested color, read back via GetBackBufferData() -- proves ClearColorAndDepth ran for real
//   rather than being masked down to a color-only clear (which only happens when
//   SupportsDepthStencil() is false).
// Check C -- ClearColorDepthAndStencil (via GraphicsDevice::Clear(ClearOptions, color, depth,
//   stencil) with all three flags) does not throw and clears color correctly -- stencil is
//   accepted and silently ignored (design decision 7), not thrown.
// Check D -- SetDepthTestEnabled/SetDepthWriteEnabled no longer throw (Phase O6 state application
//   is real) -- a smoke-level check; DirectX5_ZTest covers real depth-test pixel behavior.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"

#include "CNA/Internal/Renderers/DirectX5/DirectX5Renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX5;

static constexpr int kCanvasSize = 64;

class DirectX5Device3DSmokeTest : public Game
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
        auto& renderer = static_cast<DirectX5Renderer&>(dev.GetRenderer());

        // Check A: device bring-up succeeded -- SupportsDepthStencil() reports true.
        check(renderer.SupportsDepthStencil(), "SupportsDepthStencil() reports true after Phase O3 device bring-up");

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
        // color correctly; stencil is accepted-and-ignored (design decision 7), never thrown.
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

        // Check D: state application (Phase O6) is real -- these no longer throw.
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
            check(!threw, "SetDepthTestEnabled/SetDepthWriteEnabled do not throw (Phase O6 state application is real)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 4);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    DirectX5Device3DSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX5Device3DSmokeTest game;
    game.Run();
    return game.getResult();
}
