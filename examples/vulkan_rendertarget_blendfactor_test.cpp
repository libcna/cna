// SPDX-License-Identifier: MS-PL
// REMED-GFX-070: Vulkan GraphicsDevice.BlendFactor (the constant blend color) must be applied PER
// DRAW/BATCH, not once per frame — the Vulkan analog of GFX-069 (SdlGpu) and the blend-constant
// counterpart of GFX-062 (Viewport) / GFX-013 (Scissor).
//
// Before the fix, VulkanGraphicsBackend::RecordCommandBuffer() called vkCmdSetBlendConstants
// EXACTLY ONCE per frame — at the Phase-2 backbuffer pass begin (line ~6926), reading the live
// frame-global blendFactorR_/G_/B_/A_ members. Consequences:
//   (a) The Phase-1 render-target passes run BEFORE that single set and never set blend constants
//       themselves, so a RenderTarget2D constant-color blend used an undefined/stale blend constant.
//   (b) Multiple GraphicsDevice.BlendFactor values within one frame collapsed to the last one set
//       (last-wins), because a single record-time read cannot distinguish per-draw state.
// Vulkan declares VK_DYNAMIC_STATE_BLEND_CONSTANTS on its pipelines, so the constant is dynamic
// state that MUST be (re)issued per draw when it varies — exactly like the per-draw viewport/scissor
// GFX-062/013 already capture. The fix snapshots the factor per draw/batch at enqueue
// (PushPending3DDraw / SpriteBatch End()) and replays it via vkCmdSetBlendConstants immediately
// before the corresponding draw/batch.
//
// The blend equation makes the output depend strongly and directly on the constant:
//   ColorSourceBlend = BlendFactor, ColorDestinationBlend = Zero, source = WHITE
//     => result.rgb = white * constant = constant           (Frame 0)
//   ColorSourceBlend = InverseBlendFactor, source = WHITE
//     => result.rgb = white * (1 - constant)                (Frame 1)
//   ColorSourceBlend = One, ColorDestinationBlend = BlendFactor, dest = WHITE, source = BLACK
//     => result.rgb = black + white * constant = constant   (Frame 2, destination-side)
//
// Constant used: Color(64,128,192,153) = normalized (0.251, 0.502, 0.753, 0.600). Distinct per
// channel, so it also catches a BGRA channel swap (would read 192,128,64), an accidental white
// constant (255,255,255), and an unset/zero constant (0,0,0).
//
// Readback: Vulkan cannot download an RT via GetData (no GetTextureData impl), so — like every other
// Vulkan RT pixel test (GFX-062/013) — this renders into a RenderTarget2D, unbinds, blits the RT 1:1
// onto the backbuffer via SpriteBatch under BlendState::Opaque (so the blit itself never uses the
// constant), and reads back with GetBackBufferData. Both the RT (R8G8B8A8_UNORM) and swapchain
// (B8G8R8A8_UNORM) are linear, so midtone values survive the round trip exactly.
//
//   Frame 3 — alpha channel (two-pass, since Vulkan cannot read the RT's alpha through the blit):
//     Pass 1 writes RT.a = constant.a via AlphaSourceBlend=BlendFactor,AlphaDestinationBlend=Zero
//       (white source, RGB kept = One/Zero) => RT = (255,255,255, ~153).
//     Pass 2 draws white with ColorSourceBlend=DestinationAlpha, ColorDestinationBlend=Zero
//       => RT.rgb = white * dst.a = constant.a. So the constant's ALPHA becomes a readable gray.
//     Expected center ~= (153,153,153). A stale/white constant.a=255 would give ~255; a zero
//     constant.a=0 would give ~0.
//
//   Frame 4 — A→B→A BlendFactors in ONE render-target pass (the per-draw-capture crux): within one
//     RT pass, using ONE BlendState instance, draw three white quads into three disjoint horizontal
//     regions, changing ONLY GraphicsDevice.BlendFactor between them (RED, GREEN, RED constants).
//     Each region must show the constant active WHEN THAT draw was enqueued. The pre-fix backend
//     never sets blend constants in an RT pass at all (all three regions share one undefined/stale
//     constant); a "read the frame-global factor once at record time" fix would paint all three RED
//     (last-wins). Only genuine per-draw capture passes. This also proves the constant is dynamic
//     state, NOT a pipeline-cache key (one BlendState instance, one pipeline, three constants).
//
//   Frame 5 — constant-dependent → ordinary Opaque → the SAME constant-dependent pipeline. This
//     is GFX-091's static/dynamic pipeline-transition discriminator: the ordinary pipeline must
//     not receive a blend-constant command unless it declares that state dynamic, while the final
//     constant draw must still replay its captured RED value.
//
// SpriteBatch is covered separately by vulkan_spritebatch_blendstate_test.cpp (GFX-071/GFX-091):
// BlendFactor, InverseBlendFactor, A→B→A capture, and constant→ordinary→constant transitions.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64;   // backbuffer width
static constexpr int kBBH = 64;   // backbuffer height (!= RT height, to catch dimension leaks)
static constexpr int kRTW = 64;   // render target width
static constexpr int kRTH = 48;   // render target height (asymmetric vs width & backbuffer)

// The distinctive constant blend color. Distinct per channel; alpha < 255 for the alpha test.
static constexpr int kBFr = 64, kBFg = 128, kBFb = 192, kBFa = 153;

class VulkanRenderTargetBlendFactorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;       // used only to blit the RT to the backbuffer
    std::unique_ptr<RenderTarget2D>        rt_;
    std::unique_ptr<BasicEffect>           fx_;       // persistent: outlives every deferred draw
    int frame_ = 0;
    int pass_  = 0;
    int fail_  = 0;

    static bool near1(int v, int t, int tol = 16) { return v >= t - tol && v <= t + tol; }
    static bool isRGB(const Color& c, int r, int g, int b, int tol = 16)
    {
        return near1(c.getRProperty(), r, tol) && near1(c.getGProperty(), g, tol)
            && near1(c.getBProperty(), b, tol);
    }
    static bool isRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    static bool isGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    static bool isBlue(const Color& c)  { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), got.getAProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    // Build a BlendState with explicit color/alpha src/dst factors and a baked BlendFactor color.
    static BlendState makeBlend(Blend cSrc, Blend cDst, Blend aSrc, Blend aDst, const Color& factor)
    {
        BlendState s;
        s.setColorSourceBlendProperty(cSrc);
        s.setColorDestinationBlendProperty(cDst);
        s.setAlphaSourceBlendProperty(aSrc);
        s.setAlphaDestinationBlendProperty(aDst);
        s.setBlendFactorProperty(factor);
        return s;
    }

    // A quad spanning NDC x in [x0,x1], full y in [-1,1]; src is the vertex color emitted as the
    // blend "source" (a white source makes result = source*factor = the factor).
    void drawQuad(GraphicsDevice& dev, float x0, float x1, const Color& src)
    {
        const VertexPositionColor q[6] = {
            { Vector3(x0,  1.f, 0.f), src },
            { Vector3(x0, -1.f, 0.f), src },
            { Vector3(x1, -1.f, 0.f), src },
            { Vector3(x0,  1.f, 0.f), src },
            { Vector3(x1, -1.f, 0.f), src },
            { Vector3(x1,  1.f, 0.f), src },
        };
        fx_->Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    void beginRT(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));            // RT background = opaque black
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone); // CCW full quad needs CullNone
    }

    // Unbind the RT and blit it 1:1 onto the backbuffer (Opaque, so the blit never uses the
    // constant) so the RT's contents can be read via GetBackBufferData.
    void unbindAndBlit(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.Clear(Color(0, 0, 0, 255));
        dev.setRasterizerStateProperty(RasterizerState());
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(*rt_, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, kRTW, kRTH), Color::White);
        sb_->End();
    }

    Color readAt(GraphicsDevice& dev, int px, int py)
    {
        Color c(0, 0, 0, 0);
        Rectangle reg(px, py, 1, 1);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    // ---- per-frame render sequences (each fully idempotent so the retry loop can re-run them) ----

    void renderSourceFactor(GraphicsDevice& dev, Blend colorSrc)
    {
        beginRT(dev);
        dev.setBlendStateProperty(makeBlend(colorSrc, Blend::Zero,
                                            Blend::One, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
        drawQuad(dev, -1.f, 1.f, Color(255, 255, 255, 255));   // white source
        unbindAndBlit(dev);
    }

    void renderDestFactor(GraphicsDevice& dev)
    {
        beginRT(dev);
        dev.setBlendStateProperty(BlendState::Opaque);
        drawQuad(dev, -1.f, 1.f, Color(255, 255, 255, 255));   // dest = white
        dev.setBlendStateProperty(makeBlend(Blend::One, Blend::BlendFactor,
                                            Blend::One, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
        drawQuad(dev, -1.f, 1.f, Color(0, 0, 0, 255));         // source = black
        unbindAndBlit(dev);
    }

    void renderAlphaFactor(GraphicsDevice& dev)
    {
        beginRT(dev);
        // Pass 1: RT.a = 1*constant.a; RGB kept = white (One/Zero).
        dev.setBlendStateProperty(makeBlend(Blend::One, Blend::Zero,
                                            Blend::BlendFactor, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
        drawQuad(dev, -1.f, 1.f, Color(255, 255, 255, 255));
        // Pass 2: RT.rgb = white * dst.a = constant.a (turns the constant's alpha into readable gray).
        dev.setBlendStateProperty(makeBlend(Blend::DestinationAlpha, Blend::Zero,
                                            Blend::One, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
        drawQuad(dev, -1.f, 1.f, Color(255, 255, 255, 255));
        unbindAndBlit(dev);
    }

    void renderThreeFactorsABA(GraphicsDevice& dev)
    {
        beginRT(dev);
        // ONE BlendState instance; the set below already pushes RED. Change ONLY BlendFactor after.
        dev.setBlendStateProperty(makeBlend(Blend::BlendFactor, Blend::Zero,
                                            Blend::One, Blend::Zero, Color(255, 0, 0, 255)));
        drawQuad(dev, -1.f, -1.f / 3.f, Color(255, 255, 255, 255));   // RED constant
        dev.setBlendFactorProperty(Color(0, 255, 0, 255));
        drawQuad(dev, -1.f / 3.f, 1.f / 3.f, Color(255, 255, 255, 255)); // GREEN constant
        dev.setBlendFactorProperty(Color(255, 0, 0, 255));
        drawQuad(dev, 1.f / 3.f, 1.f, Color(255, 255, 255, 255));     // RED constant again
        unbindAndBlit(dev);
    }

    void renderPipelineTransition(GraphicsDevice& dev)
    {
        beginRT(dev);
        BlendState constant = makeBlend(Blend::BlendFactor, Blend::Zero,
                                        Blend::One, Blend::Zero, Color(255, 0, 0, 255));
        dev.setBlendStateProperty(constant);
        drawQuad(dev, -1.f, -1.f / 3.f, Color::White);                // P1: constant RED
        dev.setBlendStateProperty(BlendState::Opaque);
        drawQuad(dev, -1.f / 3.f, 1.f / 3.f, Color(0, 0, 255, 255));  // P2: ordinary BLUE
        dev.setBlendStateProperty(constant);
        drawQuad(dev, 1.f / 3.f, 1.f, Color::White);                  // P3: same P1, RED
        unbindAndBlit(dev);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTW, kRTH);
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->VertexColorEnabled = true;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const int cx = kRTW / 2, cy = kRTH / 2;

        if (frame_ == 0)
        {
            // Source-side BlendFactor: result = white * constant = constant.
            Color c(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                renderSourceFactor(dev, Blend::BlendFactor);
                c = readAt(dev, cx, cy);
                if (isRGB(c, kBFr, kBFg, kBFb)) break;
            }
            check(isRGB(c, kBFr, kBFg, kBFb), "BlendFactor src (white*constant)", c, "~(64,128,192)");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Source-side InverseBlendFactor: result = white * (1 - constant).
            Color c(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                renderSourceFactor(dev, Blend::InverseBlendFactor);
                c = readAt(dev, cx, cy);
                if (isRGB(c, 255 - kBFr, 255 - kBFg, 255 - kBFb)) break;
            }
            check(isRGB(c, 255 - kBFr, 255 - kBFg, 255 - kBFb),
                  "InverseBlendFactor src (white*(1-constant))", c, "~(191,127,63)");
            ++frame_;
            return;
        }

        if (frame_ == 2)
        {
            // Destination-side BlendFactor: black + white*constant = constant.
            Color c(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                renderDestFactor(dev);
                c = readAt(dev, cx, cy);
                if (isRGB(c, kBFr, kBFg, kBFb)) break;
            }
            check(isRGB(c, kBFr, kBFg, kBFb), "BlendFactor dst (white*constant)", c, "~(64,128,192)");
            ++frame_;
            return;
        }

        if (frame_ == 3)
        {
            // Alpha channel exposed as gray = constant.a via the two-pass DestinationAlpha trick.
            Color c(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                renderAlphaFactor(dev);
                c = readAt(dev, cx, cy);
                if (isRGB(c, kBFa, kBFa, kBFa)) break;
            }
            check(isRGB(c, kBFa, kBFa, kBFa), "BlendFactor alpha (gray=constant.a)", c, "~(153,153,153)");
            ++frame_;
            return;
        }

        if (frame_ == 4)
        {
            // State-timing crux: ONE BlendState, A→B→A constants, three disjoint regions.
            Color l(0,0,0,0), m(0,0,0,0), r(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderThreeFactorsABA(dev);
                l = readAt(dev, 10, 24);
                m = readAt(dev, 32, 24);
                r = readAt(dev, 53, 24);
                if (isRed(l) && isGreen(m) && isRed(r)) break;
            }
            check(isRed(l),   "A->B->A BlendFactor: left A  (10,24)", l, "RED");
            check(isGreen(m), "A->B->A BlendFactor: mid B   (32,24)", m, "GREEN");
            check(isRed(r),   "A->B->A BlendFactor: right A (53,24)", r, "RED");
            ++frame_;
            return;
        }

        if (frame_ == 5)
        {
            Color l(0,0,0,0), m(0,0,0,0), r(0,0,0,0);
            for (int i = 0; i < 20; ++i)
            {
                renderPipelineTransition(dev);
                l = readAt(dev, 10, 24);
                m = readAt(dev, 32, 24);
                r = readAt(dev, 53, 24);
                if (isRed(l) && isBlue(m) && isRed(r)) break;
            }
            check(isRed(l),  "pipeline P1 constant-dependent", l, "RED");
            check(isBlue(m), "pipeline P2 ordinary Opaque", m, "BLUE");
            check(isRed(r),  "pipeline P3 reused constant-dependent", r, "RED");
            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    VulkanRenderTargetBlendFactorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRenderTargetBlendFactorTest game;
    game.Run();
    return game.getResult();
}
