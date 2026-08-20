// SPDX-License-Identifier: MS-PL
// Task 709: Verify MRT (SetRenderTargets with 2+ bindings) throws clearly on SDL_Renderer.
//
// SDL_Renderer's 2D render pipeline supports exactly one active render target at a time (a
// single SDL_Texture bound via SDL_SetRenderTarget). The shared IGraphicsRenderer::SetRenderTargets
// default implementation would otherwise silently bind only the FIRST target and ignore the rest
// -- meaning a game submitting genuine MRT draws (intending separate content per target) would
// either lose data silently or have every draw land on the same single texture, with no error at
// all. Fixed by overriding SdlRenderer::SetRenderTargets to throw a clear
// std::runtime_error whenever count > 1, rather than silently misbehaving.
//
// This test found while writing it that GraphicsDeviceValidationTests.cpp's own
// SetRenderTargets_FourTargets_DoesNotThrow/_OneTarget_DoesNotThrow unit tests were ALREADY
// passing again on SDL_Renderer by the time this task started -- Tasks 704 and 708's
// GraphicsDevice.cpp fixes had already resolved their prior failure cause (an unconditional
// ClearColorAndDepth call on render-target bind), but this had gone unnoticed across Tasks
// 704-708's own regression write-ups because only the touched example-test targets and the CNA
// static library were rebuilt before each ctest run, not the separate CnaTests unit-test binary
// (ctest never builds anything itself). The TRUE SDL_Renderer known-failure baseline from Task
// 704 onward should have read 11, not 13 -- corrected here; see NEXT.md/plans/plan_graphics.md for the
// full note. SetRenderTargets_FourTargets_DoesNotThrow now needed updating for THIS task's own
// decision (added a CNA_RENDERER_SDL_RENDERER conditional expecting a throw instead, since 4
// targets now correctly throws here due to this renderer's genuine MRT limitation, not the
// separate MAX_RENDERTARGET_BINDINGS cap check that test was originally written for).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlRenderTargetsMrtThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        whiteTex_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        dev.setBlendStateProperty(BlendState::Opaque);

        RenderTarget2D rtA(dev, 4, 4);
        RenderTarget2D rtB(dev, 4, 4);
        std::vector<RenderTargetBinding> bindings{RenderTargetBinding(&rtA), RenderTargetBinding(&rtB)};

        bool threw = false;
        std::string message;
        try
        {
            dev.SetRenderTargets(bindings);
        }
        catch (const std::exception& e)
        {
            threw = true;
            message = e.what();
        }
        check(threw, "SetRenderTargets with 2 bindings throws clearly on SDL_Renderer");
        if (threw) std::printf("       message: %s\n", message.c_str());

        // The device must still be fully usable after the throw -- no half-bound, corrupted state.
        dev.SetRenderTarget(nullptr);
        dev.Clear(Color(0, 0, 0, 255));
        sb_->Begin();
        sb_->Draw(*whiteTex_, Rectangle(0, 0, W, H), Rectangle(0, 0, 1, 1), Color(0, 255, 0, 255));
        sb_->End();

        Color pixel(0, 0, 0, 0);
        const Rectangle region(W / 2, H / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getRProperty() <= 15 && pixel.getGProperty() >= 240 && pixel.getBProperty() <= 15,
              "GraphicsDevice remains fully usable after the MRT throw (backbuffer draw succeeds normally)");

        Exit();
    }

public:
    SdlRenderTargetsMrtThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlRenderTargetsMrtThrowsTest game;
    game.Run();
    return game.getResult();
}
