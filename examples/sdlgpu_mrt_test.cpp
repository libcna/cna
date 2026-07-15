// SPDX-License-Identifier: MS-PL
// plan_sdlgpu.md SDLGPU-37: Multiple Render Targets (MRT) proof for the SDL_GPU graphics backend.
//
// Stock (single fragment output) effects keep the same scope boundary this project's D3D11/D3D12
// MRT support established: rts[0] is the real draw target; rts[1..count-1] are bound and
// independently cleared but never receive a stock draw, since no STOCK shader family in this
// codebase declares more than one fragment output. Checks A-C below prove exactly that boundary.
//
// Checks D/E go further (adversarial-review finding #2: the prior version of this row was marked
// done despite this real gap) -- a custom multi-output ShaderEffect (the one kind of shader in
// this codebase that CAN genuinely declare more than one fragment output, matching how real XNA
// games actually implement G-buffer-style MRT: always via a custom Effect, never a stock one)
// drawn as a SpriteBatch sprite while 2 render targets are bound now really writes a DIFFERENT
// value to each target from the SAME single draw call, in the SAME real render pass -- this is
// what makes "several SDL_GPUColorTargetInfo entries in one render pass" real, not just Clear()
// propagation.
//
// This backend has no ReadBackbuffer() yet (SDLGPU-39's swapchain leg is a documented, unresolved
// segfault -- see that row), so verification here follows the established convention: real draws
// with no exception, plus sampling all 3 targets via SpriteBatch into distinct screen regions for
// a real screenshot (not just "didn't throw"), PLUS (new) real RenderTarget2D::GetData() pixel
// readback for the actual MRT discriminator.
//
// The render targets are members created once in LoadContent(), not Draw()-local variables --
// this backend defers all rendering to its own Present()-time EnsureFrameRendered() pass, so a
// render target destroyed before that pass runs releases its GPU texture while sprite/draw
// commands still queued against it hold the now-freed handle, a real use-after-free crash
// (discovered while first authoring this test with Draw()-local RenderTarget2D instances --
// SdlGpu_RenderTargetCube's own test avoids this by forcing an eager flush inside GetData()
// itself; this test has no such call, so the render targets must simply outlive the frame).
//
// Check A -- SetRenderTargets({rt0,rt1,rt2}) + one Clear(magenta) call clears all 3 simultaneously
//   (real MRT bind+clear), with no exception.
// Check B -- a colored3d quad drawn afterward renders with no exception; per the single-target
//   scope boundary, it should only visibly affect rt0 (confirmed via the screenshot: rt1/rt2 stay
//   magenta, rt0 turns green).
// Check C -- ClearColorAndDepth propagates to all 3 targets (depth clear included) with no
//   exception, and SetRenderTargets(nullptr, 0) cleanly restores the swapchain.
// Check D -- a custom 2-output ShaderEffect, drawn ONCE via SpriteBatch while SetRenderTargets
//   binds 2 fresh targets, writes its PRIMARY output color (a texture*tint value) into target A --
//   verified via a real GetData() readback, not just "didn't throw".
// Check E -- the SAME draw's SECOND output (a channel-swapped derivative of the primary color,
//   computed by the SAME fragment shader invocation) reads back in target B, and is NOT the same
//   value as Check D's -- the real discriminator that two genuinely distinct simultaneous
//   attachments were written by one draw, not the same value replicated or target B left untouched.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 30;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 10)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    // Same NDC technique as sdlgpu_shadereffect_test.cpp's own custom vertex shader.
    const char* kMrtVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 fragUV;
layout(set = 1, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    vec2 ndc = (inPos / pc.vpSize_pad.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    fragUV = inUV;
}
)GLSL";

    // Real 2-output fragment shader (adversarial-review finding #2's actual discriminator): output
    // 0 is texture*tint (the same value sdlgpu_shadereffect_test.cpp already proves is genuinely
    // read from pc.color); output 1 is a channel-swapped derivative of the SAME value, computed by
    // the SAME fragment invocation -- objectively different from output 0, and only reaches its
    // own distinct render target if this backend's render pass really has 2 simultaneous color
    // attachments (SDLGPU-37's own task description) rather than 1.
    const char* kMrtFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColorA;
layout(location = 1) out vec4 outColorB;
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 3, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    outColorA = texture(uTexture, fragUV) * pc.color;
    outColorB = vec4(outColorA.g, outColorA.b, outColorA.r, outColorA.a);
}
)GLSL";
}

class SdlGpuMrtTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rt0_;
    std::unique_ptr<RenderTarget2D> rt1_;
    std::unique_ptr<RenderTarget2D> rt2_;
    std::unique_ptr<VertexBuffer> quadVb_;
    // Real multi-output MRT proof (Checks D/E) -- separate targets/effect from the stock-only-
    // rt0 proof above, so the two don't entangle clear-state or draw-target bookkeeping.
    std::unique_ptr<RenderTarget2D> rtMrtA_;
    std::unique_ptr<RenderTarget2D> rtMrtB_;
    std::unique_ptr<Texture2D> whiteTex_;
    std::unique_ptr<ShaderEffect> mrtEffect_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    // Check D/E: one SpriteBatch draw with a real 2-output custom ShaderEffect, while
    // SetRenderTargets binds 2 fresh targets -- reads back both via GetData() to prove they
    // genuinely received two DIFFERENT values from the SAME draw call.
    void RunRealMrtCheck(GraphicsDevice& dev)
    {
        std::vector<RenderTargetBinding> bindings{RenderTargetBinding(rtMrtA_.get()), RenderTargetBinding(rtMrtB_.get())};
        dev.SetRenderTargets(bindings);
        dev.Clear(Color::Black);

        mrtEffect_->SetUniformVec4("color", 0.2f, 0.4f, 0.8f, 1.0f);
        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, mrtEffect_.get());
        sb.Draw(*whiteTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1), Color::White);
        sb.End();

        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color gotA(0, 0, 0, 0), gotB(0, 0, 0, 0);
        rtMrtA_->GetData(0, &centre, &gotA, 0, 1);
        rtMrtB_->GetData(0, &centre, &gotB, 0, 1);

        const Color wantA(51, 102, 204, 255);   // white * (0.2,0.4,0.8,1.0)
        const Color wantB(102, 204, 51, 255);   // channel-swapped (g,b,r,a) of wantA
        Check(Matches(gotA, wantA),
              "Check D: MRT target A got the custom shader's primary output, got=(" +
              std::to_string(gotA.getRProperty()) + "," + std::to_string(gotA.getGProperty()) + "," +
              std::to_string(gotA.getBProperty()) + ")");
        Check(Matches(gotB, wantB) && !Matches(gotB, gotA, 15),
              "Check E: MRT target B got a genuinely DIFFERENT second output from the SAME draw, got=(" +
              std::to_string(gotB.getRProperty()) + "," + std::to_string(gotB.getGProperty()) + "," +
              std::to_string(gotB.getBProperty()) + ")");
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        rt0_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        rt1_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rt2_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtA_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtB_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        const std::vector<std::uint8_t> whitePixels = {255, 255, 255, 255};
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, whitePixels));
        mrtEffect_ = std::make_unique<ShaderEffect>(dev, kMrtVertSrc, kMrtFragSrc);

        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Green }, { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
            { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
        };
        quadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadVb_->SetData(verts, 0, 6);
    }

    void RenderMrtAndSample(GraphicsDevice& dev)
    {
        std::vector<RenderTargetBinding> bindings{
            RenderTargetBinding(rt0_.get()), RenderTargetBinding(rt1_.get()), RenderTargetBinding(rt2_.get())};
        dev.SetRenderTargets(bindings);
        dev.Clear(Color::Magenta);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(quadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        dev.Clear(Color::Magenta, 1.0f);
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

        const auto& vp = dev.getViewportProperty();
        const int w = vp.getWidthProperty();
        const int h = vp.getHeightProperty();
        const int cellW = w / 3;
        dev.Clear(Color::Black);
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(*rt0_, Rectangle(0, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rt1_, Rectangle(cellW, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rt2_, Rectangle(cellW * 2, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 1)
        {
            bool threw = false;
            const char* stage = "SetRenderTargets+Clear";
            try
            {
                std::vector<RenderTargetBinding> bindings{
                    RenderTargetBinding(rt0_.get()), RenderTargetBinding(rt1_.get()), RenderTargetBinding(rt2_.get())};
                dev.SetRenderTargets(bindings);
                dev.Clear(Color::Magenta);
                Check(true, "SetRenderTargets({rt0,rt1,rt2}) + Clear(magenta) renders with no exception");

                stage = "colored3d draw";
                BasicEffect fx(dev);
                fx.VertexColorEnabled = true;
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.Apply();
                dev.SetVertexBuffer(quadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                Check(true, "colored3d quad drawn while MRT is bound renders with no exception");

                stage = "ClearColorAndDepth + SetRenderTargets(nullptr,0)";
                dev.Clear(Color::Magenta, 1.0f);
                dev.SetRenderTargets(std::vector<RenderTargetBinding>{});
                dev.Clear(Color::CornflowerBlue);
                Check(true, "ClearColorAndDepth on the MRT set + SetRenderTargets(nullptr,0) restore renders with no exception");

                stage = "real 2-output MRT draw";
                RunRealMrtCheck(dev);
            }
            catch (const std::exception& e)
            {
                threw = true;
                Check(false, std::string("threw during ") + stage + ": " + e.what());
            }
            (void)threw;

            const auto& vp = dev.getViewportProperty();
            const int w = vp.getWidthProperty();
            const int h = vp.getHeightProperty();
            const int cellW = w / 3;
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin();
            sb_->Draw(*rt0_, Rectangle(0, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->Draw(*rt1_, Rectangle(cellW, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->Draw(*rt2_, Rectangle(cellW * 2, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->End();
        }
        else
        {
            RenderMrtAndSample(dev);
        }

        if (frame_ == kTotalFrames)
        {
            std::printf("=== %d/5 PASS ===\n", passCount_);
            result_ = (passCount_ == 5) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuMrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
        static_cast<SdlGpuGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend()).SetSwapInterval(0);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuMrtTest game;
    game.Run();
    return game.getResult();
}
