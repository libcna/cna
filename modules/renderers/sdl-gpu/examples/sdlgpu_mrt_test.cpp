// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-37: Multiple Render Targets (MRT) proof for the SDL_GPU graphics renderer.
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
// This renderer has no ReadBackbuffer() yet (SDLGPU-39's swapchain leg is a documented, unresolved
// segfault -- see that row), so verification here follows the established convention: real draws
// with no exception, plus sampling all 3 targets via SpriteBatch into distinct screen regions for
// a real screenshot (not just "didn't throw"), PLUS (new) real RenderTarget2D::GetData() pixel
// readback for the actual MRT discriminator.
//
// The render targets are members created once in LoadContent(), not Draw()-local variables --
// this renderer defers all rendering to its own Present()-time EnsureFrameRendered() pass, so a
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

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

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
    // own distinct render target if this renderer's render pass really has 2 simultaneous color
    // attachments (SDLGPU-37's own task description) rather than 1.
    const char* kMrtFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColorA;
layout(location = 1) out vec4 outColorB;
layout(location = 2) out vec4 outColorC;
layout(location = 3) out vec4 outColorD;
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
    outColorC = vec4(outColorA.b, outColorA.r, outColorA.g, outColorA.a);
    outColorD = vec4(outColorA.r, outColorA.b, outColorA.g, outColorA.a);
}
)GLSL";

    std::string BuildMrtFragSource(int targetCount)
    {
        std::string source = R"GLSL(#version 450
layout(location = 0) in vec2 fragUV;
)GLSL";
        for (int i = 0; i < targetCount; ++i)
            source += "layout(location = " + std::to_string(i) + ") out vec4 outColor" + char('A' + i) + ";\n";
        source += R"GLSL(layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 3, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    outColorA = texture(uTexture, fragUV) * pc.color;
)GLSL";
        if (targetCount > 1) source += "    outColorB = vec4(outColorA.g, outColorA.b, outColorA.r, outColorA.a);\n";
        if (targetCount > 2) source += "    outColorC = vec4(outColorA.b, outColorA.r, outColorA.g, outColorA.a);\n";
        if (targetCount > 3) source += "    outColorD = vec4(outColorA.r, outColorA.b, outColorA.g, outColorA.a);\n";
        return source + "}\n";
    }
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
    std::unique_ptr<RenderTarget2D> rtMrtC_;
    std::unique_ptr<RenderTarget2D> rtMrtD_;
    std::unique_ptr<RenderTarget2D> rtMrtAltA_;
    std::unique_ptr<RenderTarget2D> rtMrtAltB_;
    std::unique_ptr<RenderTarget2D> rtMrtAltC_;
    std::unique_ptr<RenderTarget2D> rtMrtMsaaA_;
    std::unique_ptr<RenderTarget2D> rtMrtMsaaB_;
    std::unique_ptr<RenderTarget2D> rtMrtMsaaC_;
    std::unique_ptr<Texture2D> whiteTex_;
    std::array<std::unique_ptr<ShaderEffect>, 4> mrtEffects_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void RunMrtCountCheck(GraphicsDevice& dev, const std::array<RenderTarget2D*, 4>& targets,
                          int count, const std::string& label, const BlendState& blend = BlendState::Opaque)
    {
        std::vector<RenderTargetBinding> bindings;
        for (int i = 0; i < count; ++i)
            bindings.emplace_back(targets[i]);
        dev.SetRenderTargets(bindings);
        dev.Clear(Color(0, 0, 0, 0));

        // Canonical public depthless MRT state: do not let inherited device state manufacture a
        // compatibility diagnosis. SpriteBatch receives the same state explicitly below.
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        dev.setBlendStateProperty(blend);
        dev.setDepthStencilStateProperty(noDepth);
        dev.setRasterizerStateProperty(noCull);
        dev.setViewportProperty(Viewport(0, 0, kRTSize, kRTSize));
        dev.setScissorRectangleProperty(Rectangle(0, 0, kRTSize, kRTSize));

        ShaderEffect& effect = *mrtEffects_[count - 1];
        effect.SetUniformVec4("color", 0.2f, 0.4f, 0.8f, 1.0f);
        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Immediate, blend, nullptr, &noDepth, &noCull, &effect);
        sb.Draw(*whiteTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1), Color::White);
        sb.End();
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

        const std::array<Color, 4> expected{{Color(51, 102, 204, 255), Color(102, 204, 51, 255),
                                              Color(204, 51, 102, 255), Color(51, 204, 102, 255)}};
        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        bool allMatch = true;
        for (int i = 0; i < count; ++i)
        {
            Color got(0, 0, 0, 0);
            targets[i]->GetData(0, &centre, &got, 0, 1);
            allMatch = allMatch && Matches(got, expected[i]);
        }
        Check(allMatch, "custom Effect MRT " + label + " writes each active output slot exactly");
    }

    std::size_t RunStockSpriteCacheStep(GraphicsDevice& dev, const std::array<RenderTarget2D*, 4>& targets,
                                        int count, const BlendState& blend, const std::string& label)
    {
        std::vector<RenderTargetBinding> bindings;
        for (int i = 0; i < count; ++i)
            bindings.emplace_back(targets[i]);
        dev.SetRenderTargets(bindings);
        dev.Clear(Color::Magenta);
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Immediate, blend, nullptr, &noDepth, &noCull);
        sb_->Draw(*whiteTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color got0(0, 0, 0, 0);
        targets[0]->GetData(0, &centre, &got0, 0, 1);
        const bool allChannels = blend.getColorWriteChannelsProperty() == ColorWriteChannels::All;
        Check(allChannels ? Matches(got0, Color::White)
                          : got0.getRProperty() == 255,
              "stock SpriteBatch " + label + " writes slot 0 correctly");
        return static_cast<SdlGpuRenderer&>(dev.GetRenderer()).GetSpritePipelineCacheSizeEXT();
    }

    void RunMrtWriteMaskCheck(GraphicsDevice& dev)
    {
        const std::array<RenderTarget2D*, 4> targets{{rtMrtA_.get(), rtMrtB_.get(), rtMrtC_.get(), rtMrtD_.get()}};
        BlendState masks = BlendState::Opaque;
        masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        masks.setColorWriteChannels1Property(ColorWriteChannels::Green);
        masks.setColorWriteChannels2Property(ColorWriteChannels::Blue);
        masks.setColorWriteChannels3Property(ColorWriteChannels::Alpha);

        std::vector<RenderTargetBinding> bindings;
        for (RenderTarget2D* target : targets)
            bindings.emplace_back(target);
        dev.SetRenderTargets(bindings);
        dev.Clear(Color(0, 0, 0, 0));
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        ShaderEffect& effect = *mrtEffects_[3];
        effect.SetUniformVec4("color", 0.2f, 0.4f, 0.8f, 1.0f);
        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Immediate, masks, nullptr, &noDepth, &noCull, &effect);
        sb.Draw(*whiteTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1), Color::White);
        sb.End();
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        std::array<Color, 4> got{{Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)}};
        for (int i = 0; i < 4; ++i)
            targets[i]->GetData(0, &centre, &got[i], 0, 1);
        const bool exact = Matches(got[0], Color(51, 0, 0, 0)) && got[0].getAProperty() == 0
                        && Matches(got[1], Color(0, 204, 0, 0)) && got[1].getAProperty() == 0
                        && Matches(got[2], Color(0, 0, 102, 0)) && got[2].getAProperty() == 0
                        && Matches(got[3], Color(0, 0, 0, 255)) && got[3].getAProperty() == 255;
        Check(exact, "ColorWriteChannels0–3 apply to their corresponding four MRT slots");
    }

    /**
     * @brief REMED-GFX-145: TWO MRT bind cycles in ONE flush window, with the attachment SET
     *        changing between them and no `GetData` in between.
     *
     * Every other MRT check above ends its bind cycle with a `GetData`, which on this deferred
     * renderer flushes the whole frame -- so each of them is its own flush window by construction
     * and none can tell one merged native pass from two logical ones. That made the whole file
     * false-positive-capable for a defect it looks well placed to catch. Here both cycles are
     * queued before anything is read.
     *
     * `{A,B}` then `{A,C}`: the attachment set used to live on the primary's shared GPU state, so
     * the second bind rewrote the first cycle's set and `B` -- still flagged an MRT sibling, and no
     * longer in anyone's list -- was skipped by the per-target pass loop and never rendered at all.
     * Asserting B's own cycle-1 value is what catches that; asserting A's cycle-2 value is what
     * catches the two cycles being merged into one load action.
     */
    void RunMrtSegmentBoundaryCheck(GraphicsDevice& dev)
    {
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        ShaderEffect& effect = *mrtEffects_[1];  // two declared fragment outputs

        const auto cycle = [&](RenderTarget2D* extra, float r, float g, float b) {
            dev.SetRenderTargets(std::vector<RenderTargetBinding>{
                RenderTargetBinding(rtMrtAltA_.get()), RenderTargetBinding(extra)});
            dev.Clear(Color(0, 0, 0, 0));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setDepthStencilStateProperty(noDepth);
            dev.setRasterizerStateProperty(noCull);
            dev.setViewportProperty(Viewport(0, 0, kRTSize, kRTSize));
            dev.setScissorRectangleProperty(Rectangle(0, 0, kRTSize, kRTSize));
            effect.SetUniformVec4("color", r, g, b, 1.0f);
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, &noDepth, &noCull,
                     &effect);
            sb.Draw(*whiteTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1),
                    Color::White);
            sb.End();
            dev.SetRenderTargets(std::vector<RenderTargetBinding>{});
        };

        // Cycle 1 writes 0.2/0.4/0.8 -> (51,102,204); cycle 2 writes 0.8/0.2/0.4 -> (204,51,102).
        // outColorB is (g,b,r) of outColorA, so B holds (102,204,51) and C holds (51,102,204).
        cycle(rtMrtAltB_.get(), 0.2f, 0.4f, 0.8f);
        cycle(rtMrtAltC_.get(), 0.8f, 0.2f, 0.4f);

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color a(0, 0, 0, 0), b(0, 0, 0, 0), c(0, 0, 0, 0);
        rtMrtAltA_->GetData(0, &centre, &a, 0, 1);
        rtMrtAltB_->GetData(0, &centre, &b, 0, 1);
        rtMrtAltC_->GetData(0, &centre, &c, 0, 1);
        Check(Matches(a, Color(204, 51, 102, 255)),
              "MRT bind cycles in ONE flush window: attachment 0 ends at the SECOND cycle's own "
              "output, not both cycles composed into one pass");
        Check(Matches(b, Color(102, 204, 51, 255)),
              "MRT bind cycles in ONE flush window: the attachment dropped from the set still "
              "received its OWN cycle's second output");
        Check(Matches(c, Color(51, 102, 204, 255)),
              "MRT bind cycles in ONE flush window: the attachment added to the set received the "
              "second cycle's second output");
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
        rtMrtC_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtD_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtAltA_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtAltB_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtAltC_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMrtMsaaA_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        rtMrtMsaaB_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        rtMrtMsaaC_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 4, RenderTargetUsage::DiscardContents);

        const std::vector<std::uint8_t> whitePixels = {255, 255, 255, 255};
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, whitePixels));
        for (int targetCount = 1; targetCount <= 4; ++targetCount)
            mrtEffects_[targetCount - 1] = std::make_unique<ShaderEffect>(dev, kMrtVertSrc, BuildMrtFragSource(targetCount));

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

                stage = "custom Effect MRT count matrix";
                const std::array<RenderTarget2D*, 4> primary{{rtMrtA_.get(), rtMrtB_.get(), rtMrtC_.get(), rtMrtD_.get()}};
                const std::array<RenderTarget2D*, 4> alternate{{rtMrtAltA_.get(), rtMrtAltB_.get(), rtMrtAltC_.get(), nullptr}};
                const std::array<RenderTarget2D*, 4> depthBacked{{rt0_.get(), rt1_.get(), rt2_.get(), nullptr}};
                const std::array<RenderTarget2D*, 4> msaa{{rtMrtMsaaA_.get(), rtMrtMsaaB_.get(), nullptr, nullptr}};
                RunMrtCountCheck(dev, primary, 1, "one-target control");
                RunMrtCountCheck(dev, primary, 2, "two targets");
                RunMrtCountCheck(dev, primary, 3, "three targets");
                RunMrtCountCheck(dev, primary, 4, "four targets");
                RunMrtCountCheck(dev, primary, 1, "A→B→A return to one target");
                RunMrtCountCheck(dev, primary, 3, "one→three transition");
                RunMrtCountCheck(dev, primary, 1, "three→one transition");
                RunMrtCountCheck(dev, primary, 3, "three→one→three return");
                RunMrtCountCheck(dev, alternate, 3, "identical alternate target objects");
                RunMrtCountCheck(dev, depthBacked, 3, "depth-backed three targets");
                RunMrtCountCheck(dev, msaa, 2, "two targets with requested/applied " +
                                 std::to_string(rtMrtMsaaA_->getMultiSampleCountProperty()) + "x MSAA");
                auto& renderer = static_cast<SdlGpuRenderer&>(dev.GetRenderer());
                Check(renderer.GetSpritePipelineCacheSizeEXT() == 0, "MRT SpriteBatch cache begins cold");
                const std::array<RenderTarget2D*, 4> msaaThree{{rtMrtMsaaA_.get(), rtMrtMsaaB_.get(), rtMrtMsaaC_.get(), nullptr}};
                const auto cacheStep = [&](const std::array<RenderTarget2D*, 4>& targets, int count,
                                           const BlendState& blend, std::size_t expected, const std::string& label)
                {
                    const std::size_t before = renderer.GetSpritePipelineCacheSizeEXT();
                    const std::size_t after = RunStockSpriteCacheStep(dev, targets, count, blend, label);
                    std::printf("[INFO] cache %s: %zu -> %zu\n", label.c_str(), before, after);
                    Check(after == expected, "MRT cache " + label + " has the expected cardinality");
                };
                cacheStep(primary, 1, BlendState::Opaque, 1, "1 target depthless 1x (new count)");
                cacheStep(primary, 2, BlendState::Opaque, 2, "2 targets depthless 1x (new count)");
                cacheStep(primary, 3, BlendState::Opaque, 3, "3 targets depthless 1x (new count)");
                cacheStep(primary, 4, BlendState::Opaque, 4, "4 targets depthless 1x (new count)");
                cacheStep(primary, 1, BlendState::Opaque, 4, "return 1 target (reuse)");
                cacheStep(primary, 3, BlendState::Opaque, 4, "return 3 targets (reuse)");
                cacheStep(alternate, 3, BlendState::Opaque, 4, "alternate objects 3 targets (reuse)");
                cacheStep(depthBacked, 3, BlendState::Opaque, 5, "3 targets depth-backed (new depth)");
                cacheStep(primary, 3, BlendState::Opaque, 5, "return depthless 3 targets (reuse)");
                cacheStep(msaaThree, 3, BlendState::Opaque, 6, "3 targets applied MSAA (new samples)");
                cacheStep(primary, 3, BlendState::Opaque, 6, "return 1x 3 targets (reuse)");
                BlendState masks = BlendState::Opaque;
                masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
                masks.setColorWriteChannels1Property(ColorWriteChannels::Green);
                masks.setColorWriteChannels2Property(ColorWriteChannels::Blue);
                cacheStep(primary, 3, masks, 7, "3 targets per-slot masks (new write state)");
                cacheStep(primary, 3, BlendState::Opaque, 7, "return all write masks (reuse)");
                RunMrtWriteMaskCheck(dev);
                stage = "MRT bind-cycle boundaries in one flush window";
                RunMrtSegmentBoundaryCheck(dev);
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
            std::printf("=== %d/45 PASS ===\n", passCount_);
            result_ = (passCount_ == 45) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuMrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
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
