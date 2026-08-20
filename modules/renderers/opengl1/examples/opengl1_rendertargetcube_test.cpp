// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: RenderTargetCube support (plans/plan_opengl1.md item 24, EasyGL parity).
//
// Before this, CreateRenderTargetCube() inherited the IGraphicsRenderer nullptr default --
// RenderTargetCube always failed to construct a usable renderer on OPENGL1, despite this renderer
// already having both pieces needed to combine it (FBO render targets, item 2; cube-map textures,
// item 5).
//
// Method: render two DIFFERENT solid colors into two DIFFERENT faces of the same
// RenderTargetCube (+X red, +Y blue), unbind, then read both faces back via
// RenderTargetCube::GetData() (a genuine GPU readback -- OpenGL1RenderTargetCubeRenderer::GetData()
// via glGetTexImage, not a CPU shadow) and confirm each face independently kept its own distinct
// content -- not just "something got painted somewhere" (which a single-face test could not rule
// out an all-faces-aliased bug). A third, never-rendered face is also checked to not have picked
// up either color, confirming Build()'s per-face pre-allocation didn't leak stale/garbage content
// across faces. Also exercises DebugSimulateContextLoss() while the target is genuinely bound and
// active (matching the established phase-8/item-8 rigor for RenderTarget2D/TextureCube) -- the
// bound face must survive being rebuilt and re-bound, with fresh content immediately drawable.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    bool IsColor(const Color& c, int r, int g, int b)
    {
        return std::abs(c.getRProperty() - r) <= 10 &&
               std::abs(c.getGProperty() - g) <= 10 &&
               std::abs(c.getBProperty() - b) <= 10;
    }
}

class OpenGL1RenderTargetCubeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        RenderTargetCube rt(dev, kSize, /*mipMap=*/false, SurfaceFormat::Color, DepthFormat::None);

        dev.SetRenderTarget(&rt, CubeMapFace::PositiveX);
        dev.Clear(Color(255, 0, 0, 255));
        dev.SetRenderTarget(&rt, CubeMapFace::PositiveY);
        dev.Clear(Color(0, 0, 255, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color px[1] = { Color(0, 0, 0, 0) };
        const Rectangle one(kSize / 2, kSize / 2, 1, 1);

        rt.GetData(CubeMapFace::PositiveX, 0, &one, px, 0, 1);
        std::printf("+X face centre=(%d,%d,%d)\n", px[0].getRProperty(), px[0].getGProperty(), px[0].getBProperty());
        Check(IsColor(px[0], 255, 0, 0), "+X face reads back red (what was actually drawn into it)");

        rt.GetData(CubeMapFace::PositiveY, 0, &one, px, 0, 1);
        std::printf("+Y face centre=(%d,%d,%d)\n", px[0].getRProperty(), px[0].getGProperty(), px[0].getBProperty());
        Check(IsColor(px[0], 0, 0, 255), "+Y face reads back blue -- independently addressable, "
              "not aliased with +X (would also read red if faces shared storage/attachment)");

        rt.GetData(CubeMapFace::NegativeX, 0, &one, px, 0, 1);
        std::printf("-X face (never rendered) centre=(%d,%d,%d)\n", px[0].getRProperty(), px[0].getGProperty(), px[0].getBProperty());
        Check(!IsColor(px[0], 255, 0, 0) && !IsColor(px[0], 0, 0, 255),
              "-X face (never rendered) picked up neither +X's red nor +Y's blue -- Build()'s "
              "per-face pre-allocation didn't leak content across faces");

        // Context-loss recovery, matching the established RenderTarget2D/TextureCube rigor: bind
        // a face, trigger the loss WHILE it's active, then confirm fresh content is immediately
        // drawable after recovery (not silently landing on the backbuffer / a stale handle).
        dev.SetRenderTarget(&rt, CubeMapFace::NegativeY);
        dev.Clear(Color(0, 255, 0, 255));
        dev.GetRenderer().DebugSimulateContextLoss();
        dev.Clear(Color(0, 255, 0, 255)); // Re-draw: the pre-loss draw's content is not expected
                                           // to survive (GPU-only, no CPU shadow -- documented,
                                           // same as every other OPENGL1 render target), only that
                                           // the target is still genuinely bound and usable.
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        rt.GetData(CubeMapFace::NegativeY, 0, &one, px, 0, 1);
        std::printf("-Y face after context loss centre=(%d,%d,%d)\n", px[0].getRProperty(), px[0].getGProperty(), px[0].getBProperty());
        Check(IsColor(px[0], 0, 255, 0), "-Y face survives DebugSimulateContextLoss() while "
              "actively bound -- rebuilt, re-bound, and immediately drawable again");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1RenderTargetCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1RenderTargetCubeTest game;
    game.Run();
    return game.getResult();
}
