// SPDX-License-Identifier: MS-PL
// Task 764: verify GraphicsDevice.ReferenceStencil (a real, independent device property, set
// standalone via GraphicsDevice::setReferenceStencilProperty(), separate from a full
// DepthStencilState re-application) actually reaches Bgfx draw calls.
//
// Bgfx-specific adaptation of examples/easygl_graphicsdevice_reference_stencil_test.cpp (Task
// 319, already reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 2
// spatially-separate regions in the SAME rendered frame, but Bgfx's own GetBackBufferData only
// reliably reflects the FIRST read per rendered frame (Task 406 finding) -- restructured into one
// separately-read RunCheck() pass per check, mirroring Task 759-763's established Bgfx pattern.
//
// Method per check: stamp the whole screen to stencil=0x05, then set a DepthStencilState ONCE
// with StencilFunction=Equal/ReferenceStencil=0x05 baked in (matches the stamp), then OPTIONALLY
// call GraphicsDevice::setReferenceStencilProperty() standalone to change JUST the reference
// value before the test draw, without touching DepthStencilState again.
//
// Check A (baseline, no standalone change): draw the test quad with the DepthStencilState as-is
//   (baked-in ReferenceStencil=0x05, matches the stamp) -> stencil test PASSES -> GREEN.
// Check B (standalone ReferenceStencil change): after setting the SAME DepthStencilState as check
//   A, call setReferenceStencilProperty(0x99) directly (NOT via a new DepthStencilState) -- if
//   this genuinely reaches the draw, the actual reference used is now 0x99, and 0x99 != 0x05 ->
//   stencil test FAILS -> BACKGROUND. If GraphicsDevice.ReferenceStencil is silently ignored on
//   this renderer (a no-op, as Bgfx's IGraphicsRenderer::SetReferenceStencil base default was before
//   this task), the draw would incorrectly still use the stale baked-in 0x05 -> incorrectly GREEN.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    void DrawQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeStampState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(0x05);
        return ds;
    }

    DepthStencilState MakeTestState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(0x05);  // baked-in reference, matches the stamp
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }
}

class BgfxGraphicsDeviceReferenceStencilTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }

    Color RunCheck(GraphicsDevice& dev, bool overrideReferenceStandalone)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
            dev.setBlendStateProperty(BlendState::Opaque);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuad(dev, kBackground);

            dev.setDepthStencilStateProperty(MakeTestState());
            if (overrideReferenceStandalone)
                dev.setReferenceStencilProperty(0x99);
            DrawQuad(dev, kGreen);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { const char* name; bool overrideStandalone; bool expectGreen; };
        const Check checks[2] = {
            { "Baseline (baked-in ReferenceStencil=0x05, matches stamp)",            false, true  },
            { "Standalone ReferenceStencil=0x99 override (must reach the draw)",     true,  false },
        };

        int passCount = 0;
        for (const auto& check : checks)
        {
            const Color c = RunCheck(dev, check.overrideStandalone);
            const bool sawGreen = IsGreen(c);
            const bool ok = check.expectGreen ? sawGreen : !sawGreen;
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", check.name,
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        check.expectGreen ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    BgfxGraphicsDeviceReferenceStencilTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxGraphicsDeviceReferenceStencilTest game;
    game.Run();
    return game.getResult();
}
