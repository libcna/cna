// SPDX-License-Identifier: MS-PL
// Task 774: verify MRT with mixed formats is rejected or handled per XNA constraints on Bgfx.
//
// Research finding (this task's own investigation, confirmed via source, not assumed): FNA's own
// real GraphicsDevice.SetRenderTargets (src/Graphics/GraphicsDevice.cs) performs ZERO surface-
// format validation at all -- it forwards every RenderTargetBinding straight to FNA3D with no
// format-compatibility check whatsoever, so there is no "FNA constraint on mixed MRT formats" to
// match at the SetRenderTargets call site itself. The real, already-shipped, already-tested
// constraint lives one level earlier: Texture::ValidateFormat (Texture.cpp), called unconditionally
// by every format-taking Texture2D/Texture3D/TextureCube constructor (and therefore
// RenderTarget2D's own full constructor, which delegates to Texture2D's) on ALL FOUR backends,
// already throws std::runtime_error for every SurfaceFormat value except Color (Task 176's
// contract, confirmed in examples/easygl_surface_format_throws_test.cpp -- registered for EasyGL
// only before this task, reused verbatim here for Bgfx below since it is 100% backend-agnostic,
// pure CPU-side construction, no rendering at all).
//
// This means "MRT with mixed formats" is not merely rejected at bind time -- it is IMPOSSIBLE TO
// EVEN CONSTRUCT anywhere in this codebase today: every RenderTarget2D that could ever reach
// SetRenderTargets is necessarily SurfaceFormat::Color, since any other format throws at
// construction, long before two such render targets could ever be bound together. This test
// confirms that directly for RenderTarget2D specifically (not just the more general Texture2D
// case the reused EasyGL test covers): attempting RenderTarget2D(..., SurfaceFormat::Bgr565, ...)
// throws before SetRenderTargets is ever reached, on Bgfx.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BgfxMrtMixedFormatsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;
    int pass_   = 0;
    int fail_   = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        ok ? ++pass_ : ++fail_;
    }

    void Draw(const GameTime&) override {}

protected:
    void Initialize() override
    {
        Game::Initialize();
        GraphicsDevice& dev = getGraphicsDeviceProperty();

        // Baseline: two Color-format RTs construct fine and CAN be bound together (same format,
        // the only format this codebase's shared ValidateFormat contract ever allows).
        bool sameFormatOk = true;
        try
        {
            RenderTarget2D rtA(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
            RenderTarget2D rtB(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTargets({ RenderTargetBinding(&rtA), RenderTargetBinding(&rtB) });
            dev.SetRenderTargets({});
        }
        catch (const std::exception& e)
        {
            sameFormatOk = false;
            std::printf("       (same-format MRT bind threw unexpectedly: %s)\n", e.what());
        }
        check(sameFormatOk, "two SurfaceFormat::Color RenderTarget2D instances bind together via SetRenderTargets without throwing");

        // The real ask: a non-Color-format RenderTarget2D must throw at CONSTRUCTION time --
        // proving mixed-format MRT can never even be attempted, since the second (differently
        // formatted) render target never comes into existence for SetRenderTargets to bind.
        bool mixedFormatThrew = false;
        try
        {
            RenderTarget2D rtColor(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
            RenderTarget2D rtMixed(dev, 4, 4, false, SurfaceFormat::Bgr565, DepthFormat::None);
            // Unreachable if the line above throws, as expected.
            dev.SetRenderTargets({ RenderTargetBinding(&rtColor), RenderTargetBinding(&rtMixed) });
            dev.SetRenderTargets({});
        }
        catch (const std::runtime_error&)
        {
            mixedFormatThrew = true;
        }
        check(mixedFormatThrew, "RenderTarget2D(..., SurfaceFormat::Bgr565, ...) throws std::runtime_error at construction -- mixed-format MRT is unreachable, not merely rejected at bind time");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        result_ = (fail_ == 0) ? 0 : 1;
        Exit();
    }

public:
    BgfxMrtMixedFormatsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxMrtMixedFormatsTest game;
    game.Run();
    return game.getResult();
}
