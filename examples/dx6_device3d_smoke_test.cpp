// SPDX-License-Identifier: MS-PL
// plan_dx2.md Phase O3 (DX2-20..DX2-26): smoke test for DX6's real Direct3D v3 device bring-up (a port of DX5's own, itself a port of DX30/DX2's) --
// IDirect3D3/IDirect3DDevice3/IDirect3DViewport3 creation against the shadow-backbuffer surface,
// a real attached 32-bit depth+stencil Z-buffer (plan_dx6.md design decision 4), and the
// ClearColorAndDepth/ClearDepth/ClearStencil/etc entry points. VertexBuffer/IndexBuffer storage,
// the 3D draw path, and state application (SetDepthTestEnabled/ApplyRasterizerState/etc) are all
// real now -- pixel-verified 3D rendering is covered by dx6_colored_primitives_test.cpp/
// dx6_ztest_test.cpp/etc, and real stencil write-then-test is covered by dx6_stencil_test.cpp, not
// this file. Check D below only confirms the simple state-toggle methods don't throw (a
// smoke-level check).
//
// Check A -- backend.SupportsDepthStencil() reports true (device bring-up succeeded; DX1 always
//   reports false here).
// Check B -- GraphicsDevice::Clear(color, depth) (the two-arg overload, which requests
//   Target|DepthBuffer) does not throw and correctly clears the color buffer to the exact
//   requested color, read back via GetBackBufferData() -- proves ClearColorAndDepth ran for real
//   rather than being masked down to a color-only clear (which only happens when
//   SupportsDepthStencil() is false).
// Check C -- ClearColorDepthAndStencil (via GraphicsDevice::Clear(ClearOptions, color, depth,
//   stencil) with all three flags) does not throw and clears color correctly -- DX6's stencil
//   clear is real (design decision 4/5, via Clear2 D3DCLEAR_STENCIL), but this smoke test only
//   checks the color result; dx6_stencil_test.cpp is the dedicated real stencil write/test proof.
// Check D -- SetDepthTestEnabled/SetDepthWriteEnabled no longer throw -- a smoke-level check;
//   Dx6_ZTest covers real depth-test pixel behavior.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"

#include "CNA/Internal/Backends/Dx6/Dx6GraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Dx6;

static constexpr int kCanvasSize = 64;

class Dx6Device3DSmokeTest : public Game
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
        auto& backend = static_cast<Dx6GraphicsBackend&>(dev.GetBackend());

        // Check A: device bring-up succeeded -- SupportsDepthStencil() reports true.
        check(backend.SupportsDepthStencil(), "SupportsDepthStencil() reports true after Phase O3 device bring-up");

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
        // color correctly; DX6's stencil clear is real (see dx6_stencil_test.cpp for the
        // dedicated write/test proof).
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
                backend.SetDepthTestEnabled(true);
                backend.SetDepthWriteEnabled(true);
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
    Dx6Device3DSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx6Device3DSmokeTest game;
    game.Run();
    return game.getResult();
}
