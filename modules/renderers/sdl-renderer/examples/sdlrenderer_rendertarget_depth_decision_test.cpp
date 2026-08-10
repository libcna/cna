// SPDX-License-Identifier: MS-PL
// Task 708: Decide and document render-target depth-buffer behavior on SDL_Renderer.
//
// Decision: SDL_Renderer's render targets SILENTLY IGNORE any requested DepthFormat rather than
// throwing. SdlRenderer::CreateRenderTarget2D never allocates real depth-stencil storage
// regardless of what was requested (Task 704's own finding); RenderTarget2D::DepthStencilFormat
// still echoes back whatever was requested verbatim, matching FNA's own plain field-store
// semantics (no device-capability clamping there either -- see RenderTarget2D.cs). Rationale for
// silently ignoring rather than throwing: SDL_Renderer's 2D sprite pipeline never performs actual
// depth testing under ANY circumstance regardless of which render target is bound (SpriteBatch
// never depth-tests on any renderer, and this renderer's `SetDepthTestEnabled`/`ClearDepth`/
// `ClearColorAndDepth` already throw unconditionally the moment anything actually TRIES to use
// depth testing) -- so a caller requesting a depth format for portability with the other 3
// (depth-capable) renderers, without ever actually issuing a depth-testing draw call, hits no
// functional difference on this renderer and should not be penalized with a throw for an inert,
// unactionable request.
//
// This task found and fixed a real bug while verifying that decision: GraphicsDevice::
// SetRenderTarget's Task 704 fix decided whether to include ClearOptions::DepthBuffer in the
// auto-clear-on-bind by checking the XNA-level REQUESTED DepthStencilFormat property -- which
// reflects what the CALLER asked for, not whether the renderer actually allocated real storage.
// For SDL_Renderer, which NEVER allocates one regardless of request, this meant simply
// constructing a RenderTarget2D with a real depth format (e.g. Depth24Stencil8) and binding it
// with the default DiscardContents usage crashed immediately via
// ClearColorAndDepth's unconditional ThrowNo3D. Fixed by adding
// IRenderTargetRenderer::HasRealDepthBuffer(bool depthFormatWasRequested), defaulting to mirror the
// requested format (zero behavior change for EasyGL/Vulkan/Bgfx, which DO honor requested depth
// formats), with SdlRenderTargetRenderer overriding it to always return false.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlRenderTargetDepthDecisionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

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
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);

        // A RenderTarget2D requesting a real depth format must construct without throwing --
        // SDL_Renderer just never backs it with real storage.
        RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8);
        check(rt.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              "DepthStencilFormat echoes the requested value verbatim (FNA: plain field-store, no device clamping)");

        // Binding it (default DiscardContents usage -> triggers the auto-clear-on-bind path)
        // must NOT throw, even though a real depth format was requested.
        bool bindThrew = false;
        std::string bindError;
        try
        {
            dev.SetRenderTarget(&rt);
        }
        catch (const std::exception& e)
        {
            bindThrew = true;
            bindError = e.what();
        }
        check(!bindThrew, "Binding a RenderTarget2D with a real requested DepthFormat does not throw on SDL_Renderer");
        if (bindThrew) std::printf("       threw: %s\n", bindError.c_str());

        dev.Clear(Color(255, 0, 255, 255));
        dev.SetRenderTarget(nullptr);
        check(true, "Draw/unbind cycle with a real-depth-format render target completes without incident");

        // Decision boundary: genuinely ATTEMPTING to use depth testing must still throw --
        // ignoring the requested DepthFormat must not silently unlock functionality that this
        // 2D-only renderer never actually has.
        bool clearDepthThrew = false;
        try
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);
        }
        catch (const std::exception&)
        {
            clearDepthThrew = true;
        }
        check(clearDepthThrew,
              "Explicitly requesting a depth-buffer clear on the (real, unbound) backbuffer still throws -- ignoring RenderTarget2D's DepthFormat doesn't unlock real depth support");

        Exit();
    }

public:
    SdlRenderTargetDepthDecisionTest()
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
    SdlRenderTargetDepthDecisionTest game;
    game.Run();
    return game.getResult();
}
