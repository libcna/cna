// SPDX-License-Identifier: MS-PL
// Rejection test for the TinyGL renderer. Every check here asserts that a request TinyGL cannot
// execute is REFUSED with System::NotSupportedException rather than accepted and quietly not
// performed.
//
// This suite matters more for TINYGL than for a GPU-backed renderer: TinyGL answers an argument
// combination it cannot handle by calling gl_fatal_error(), which terminates the process instead of
// setting an error flag (TINYGL-0, tinygl-spike/README.md). Validation before the native call is
// therefore the only thing standing between a bad argument and a dead process, and these checks
// are what keep that validation in place.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <functional>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::TinyGL;

namespace
{
    constexpr int kChecks = 13;
}

class TinyGLRejectionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    /// Passes when @p action throws System::NotSupportedException, and only that.
    void expectRefusal(const std::function<void()>& action, const char* label)
    {
        bool refused = false;
        try { action(); }
        catch (const System::NotSupportedException&) { refused = true; }
        catch (...) { refused = false; }
        check(refused, label);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<TinyGLRenderer&>(dev.GetRenderer());

        // --- Stencil: TinyGL's ZBuffer has no stencil plane at all -------------------------------
        expectRefusal([&] { renderer.ClearStencil(0); },
                      "ClearStencil() is refused -- TinyGL has no stencil plane");
        expectRefusal([&] { renderer.ClearDepthAndStencil(1.0f, 0); },
                      "ClearDepthAndStencil() is refused");
        expectRefusal([&] { renderer.ClearColorDepthAndStencil(0, 0, 0, 1, 1.0f, 0); },
                      "ClearColorDepthAndStencil() is refused");
        expectRefusal([&] { renderer.SetReferenceStencil(3); },
                      "a non-zero ReferenceStencil is refused");
        expectRefusal([&] {
                          DepthStencilState state;
                          state.setStencilEnableProperty(true);
                          dev.setDepthStencilStateProperty(state);
                      },
                      "enabling the stencil test is refused");

        // --- Depth comparison: TinyGL implements no glDepthFunc ----------------------------------
        expectRefusal([&] {
                          DepthStencilState state;
                          state.setDepthBufferEnableProperty(true);
                          state.setDepthBufferFunctionProperty(CompareFunction::GreaterEqual);
                          dev.setDepthStencilStateProperty(state);
                      },
                      "a depth CompareFunction other than Less is refused");

        // --- Blending: only the factors TinyGL's rasterizer switch really has cases for ----------
        expectRefusal([&] { dev.setBlendStateProperty(BlendState::Additive); },
                      "BlendState::Additive (SourceAlpha, One) is refused");
        expectRefusal([&] {
                          BlendState state;
                          state.setColorSourceBlendProperty(Blend::DestinationColor);
                          state.setAlphaSourceBlendProperty(Blend::DestinationColor);
                          state.setColorDestinationBlendProperty(Blend::Zero);
                          state.setAlphaDestinationBlendProperty(Blend::Zero);
                          dev.setBlendStateProperty(state);
                      },
                      "an unexecutable source Blend factor is refused");
        expectRefusal([&] {
                          BlendState state;
                          state.setColorSourceBlendProperty(Blend::One);
                          state.setAlphaSourceBlendProperty(Blend::One);
                          state.setColorDestinationBlendProperty(Blend::One);
                          state.setAlphaDestinationBlendProperty(Blend::One);
                          state.setColorBlendFunctionProperty(BlendFunction::Max);
                          state.setAlphaBlendFunctionProperty(BlendFunction::Max);
                          dev.setBlendStateProperty(state);
                      },
                      "BlendFunction::Max is refused -- TinyGL has Add/Subtract/ReverseSubtract only");
        expectRefusal([&] {
                          BlendState state;
                          state.setColorWriteChannelsProperty(ColorWriteChannels::Red);
                          dev.setBlendStateProperty(state);
                      },
                      "a partial ColorWriteChannels mask is refused -- TinyGL has no glColorMask");

        // --- Rasterizer: no glScissor, and glPolygonOffset stores without applying ---------------
        expectRefusal([&] {
                          RasterizerState state;
                          state.setScissorTestEnableProperty(true);
                          dev.setRasterizerStateProperty(state);
                      },
                      "ScissorTestEnable is refused -- TinyGL implements no glScissor");
        expectRefusal([&] {
                          RasterizerState state;
                          state.setDepthBiasProperty(0.001f);
                          dev.setRasterizerStateProperty(state);
                      },
                      "a non-zero DepthBias is refused -- TinyGL's glPolygonOffset is inert");

        // --- Render targets: TinyGL owns exactly one framebuffer per context ---------------------
        expectRefusal([&] { renderer.SetRenderTarget2D(reinterpret_cast<IRenderTargetRenderer*>(1)); },
                      "binding a render target is refused");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    TinyGLRejectionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    TinyGLRejectionTest game;
    game.Run();
    return game.getResult();
}
