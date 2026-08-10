// SPDX-License-Identifier: MS-PL
// REMED-GFX-069: SdlGpu GraphicsDevice.BlendFactor (the constant blend color) must reach the GPU
// for blend modes that use Blend::BlendFactor / Blend::InverseBlendFactor.
//
// SdlGpu maps Blend::BlendFactor(10) -> SDL_GPU_BLENDFACTOR_CONSTANT_COLOR and
// Blend::InverseBlendFactor(11) -> SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR in its pipeline
// (ToBlendFactor/FillBlendState), but before this fix it never overrode the no-op base
// IGraphicsRenderer::SetBlendFactor and never called SDL_SetGPUBlendConstants, so constant-color
// blends used SDL_gpu's default blend constant (0,0,0,0) instead of GraphicsDevice.BlendFactor.
// This is the blend-constant analog of GFX-064 (Viewport) / GFX-068 (Scissor): the constant must
// be captured PER DRAW at enqueue and replayed per draw, because SdlGpu is a DEFERRED renderer that
// queues every draw and replays it at Present-time — a single stored value read at Present would
// give every queued draw the LAST BlendFactor set that frame.
//
// The blend equation makes the output depend strongly and directly on the constant:
//   ColorSourceBlend = BlendFactor, ColorDestinationBlend = Zero, source = WHITE
//     => result.rgb = white * constant = constant           (Frame 0)
//   ColorSourceBlend = InverseBlendFactor, source = WHITE
//     => result.rgb = white * (1 - constant)                (Frame 1)
//   ColorSourceBlend = One, ColorDestinationBlend = BlendFactor, dest = WHITE, source = BLACK
//     => result.rgb = black + white * constant = constant   (Frame 2, destination-side)
//   AlphaSourceBlend = BlendFactor, AlphaDestinationBlend = Zero, source.a = 1
//     => result.a   = 1 * constant.a                        (Frame 3, alpha channel)
//
// Constant used: Color(64,128,192,153) = normalized (0.251, 0.502, 0.753, 0.600). Distinct per
// channel, so it also catches a BGRA channel swap (would read 192,128,64), an accidental white
// constant (255,255,255), and the pre-fix default-zero constant (0,0,0).
//
// Readback: SdlGpu cannot download the swapchain (SDLGPU-39), so — like every other SdlGpu pixel
// test — this renders into a RenderTarget2D and reads it back with RenderTarget2D::GetData() AFTER
// unbind (the realistic deferred-replay case).
//
//   Frame 4 — three BlendFactors in ONE pass (the per-draw-capture crux, Phase 7): within one RT
//     pass, using ONE BlendState instance, draw three white quads into three disjoint horizontal
//     regions, changing ONLY GraphicsDevice.BlendFactor between them (RED, GREEN, BLUE constants).
//     Each region must show the constant active WHEN THAT draw was enqueued. A solution that stores
//     one final BlendFactor and reads it at Present would paint all three the same (BLUE), and the
//     pre-fix renderer paints them all BLACK (constant stuck at 0). This simultaneously proves
//     Phase 15 (changing only BlendFactor, not BlendState, changes the output) and Phase 16 (the
//     constant is dynamic state, NOT baked into the pipeline-cache key — one BlendState instance,
//     one pipeline, three different constants).
//
//   Frame 5 — SpriteBatch (Phase 17): a white texture drawn through SpriteBatch.Begin() with a
//     custom BlendState whose baked BlendFactor is (64,128,192). Sprites enqueue through the same
//     PushDrawOrder/QueuedDrawRef path as 3D draws, so the same per-draw blend-constant capture
//     must service them; expected center = the constant.
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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 96;   // backbuffer width  (!= RT, to catch dimension leaks)
static constexpr int kBBH = 72;   // backbuffer height
static constexpr int kRTW = 64;   // render target width  (asymmetric vs height & backbuffer)
static constexpr int kRTH = 48;   // render target height

// The distinctive constant blend color. Distinct per channel; alpha < 255 for the alpha test.
static constexpr int kBFr = 64, kBFg = 128, kBFb = 192, kBFa = 153;

class SdlGpuRenderTargetBlendFactorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D>        rt_;
    std::unique_ptr<VertexBuffer>          whiteQuadVb_;
    std::unique_ptr<VertexBuffer>          blackQuadVb_;
    std::unique_ptr<VertexBuffer>          leftQuadVb_;
    std::unique_ptr<VertexBuffer>          midQuadVb_;
    std::unique_ptr<VertexBuffer>          rightQuadVb_;
    std::unique_ptr<BasicEffect>           fx_;   // must outlive every draw (GPU resources)
    std::unique_ptr<Texture2D>             whiteTex_;
    std::unique_ptr<SpriteBatch>           sb_;
    int frame_ = 0;
    int pass_  = 0;
    int fail_  = 0;

    // Channel-band predicates: value within +-tol of target.
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

    static std::unique_ptr<VertexBuffer> makeQuad(GraphicsDevice& dev, float x0, float x1, const Color& col)
    {
        // A quad spanning NDC x in [x0,x1], full y in [-1,1]. col is the vertex color the shader
        // emits as the blend "source"; a white source makes result = source*factor = the factor.
        const VertexPositionColor q[6] = {
            { Vector3(x0,  1.f, 0.f), col },
            { Vector3(x0, -1.f, 0.f), col },
            { Vector3(x1, -1.f, 0.f), col },
            { Vector3(x0,  1.f, 0.f), col },
            { Vector3(x1, -1.f, 0.f), col },
            { Vector3(x1,  1.f, 0.f), col },
        };
        auto vb = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(),
                                                 6, BufferUsage::None);
        vb->SetData(q, 0, 6);
        return vb;
    }

    void drawQuad(GraphicsDevice& dev, VertexBuffer& vb)
    {
        fx_->Apply();   // fx_ (identity MVP, VertexColorEnabled) must outlive the draw below
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Build a BlendState with explicit color/alpha src/dst and a baked BlendFactor.
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

    Color readAt(int px, int py)
    {
        Color c(0, 0, 0, 0);
        const Rectangle reg(px, py, 1, 1);
        rt_->GetData(0, &reg, &c, 0, 1);
        return c;
    }

    void beginRTFrame(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));            // RT background = opaque black
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone); // CCW full quad needs CullNone
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTW, kRTH);
        const Color W(255, 255, 255, 255);
        whiteQuadVb_ = makeQuad(dev, -1.f, 1.f, W);
        blackQuadVb_ = makeQuad(dev, -1.f, 1.f, Color(0, 0, 0, 255));
        // Three disjoint horizontal regions for the state-timing frame.
        leftQuadVb_  = makeQuad(dev, -1.f,      -1.f / 3.f, W);
        midQuadVb_   = makeQuad(dev, -1.f / 3.f,  1.f / 3.f, W);
        rightQuadVb_ = makeQuad(dev,  1.f / 3.f,  1.f,       W);
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->VertexColorEnabled = true;

        const std::vector<std::uint8_t> whitePixels(2 * 2 * 4, 255); // 2x2 RGBA, all 0xFF = white
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 2, 2, whitePixels));
        sb_ = std::make_unique<SpriteBatch>(dev);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 0)
        {
            // Source-side BlendFactor: result = white * constant = constant.
            beginRTFrame(dev);
            dev.setBlendStateProperty(makeBlend(Blend::BlendFactor, Blend::Zero,
                                                Blend::One, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
            drawQuad(dev, *whiteQuadVb_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color c = readAt(kRTW / 2, kRTH / 2);
            check(isRGB(c, kBFr, kBFg, kBFb), "BlendFactor src (white*constant)", c, "~(64,128,192)");
            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // Source-side InverseBlendFactor: result = white * (1 - constant).
            beginRTFrame(dev);
            dev.setBlendStateProperty(makeBlend(Blend::InverseBlendFactor, Blend::Zero,
                                                Blend::One, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
            drawQuad(dev, *whiteQuadVb_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color c = readAt(kRTW / 2, kRTH / 2);
            check(isRGB(c, 255 - kBFr, 255 - kBFg, 255 - kBFb), "InverseBlendFactor src (white*(1-constant))",
                  c, "~(191,127,63)");
            ++frame_;
            return;
        }

        if (frame_ == 2)
        {
            // Destination-side BlendFactor: background WHITE (Opaque), then source BLACK with
            // ColorSrc=One, ColorDst=BlendFactor => result = black + white*constant = constant.
            beginRTFrame(dev);
            dev.setBlendStateProperty(BlendState::Opaque);
            drawQuad(dev, *whiteQuadVb_);                      // dest = white
            dev.setBlendStateProperty(makeBlend(Blend::One, Blend::BlendFactor,
                                                Blend::One, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
            drawQuad(dev, *blackQuadVb_);                      // source = black
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color c = readAt(kRTW / 2, kRTH / 2);
            check(isRGB(c, kBFr, kBFg, kBFb), "BlendFactor dst (white*constant)", c, "~(64,128,192)");
            ++frame_;
            return;
        }

        if (frame_ == 3)
        {
            // Alpha channel: AlphaSrc=BlendFactor, AlphaDst=Zero => result.a = 1*constant.a.
            // RGB kept = source (One/Zero) so only alpha probes the constant's alpha.
            beginRTFrame(dev);
            dev.setBlendStateProperty(makeBlend(Blend::One, Blend::Zero,
                                                Blend::BlendFactor, Blend::Zero, Color(kBFr, kBFg, kBFb, kBFa)));
            drawQuad(dev, *whiteQuadVb_);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color c = readAt(kRTW / 2, kRTH / 2);
            check(near1(c.getAProperty(), kBFa), "BlendFactor alpha (1*constant.a)", c, "A~153");
            ++frame_;
            return;
        }

        if (frame_ == 4)
        {
            // State-timing crux: ONE BlendState instance, three constants, three regions.
            beginRTFrame(dev);
            dev.setBlendStateProperty(makeBlend(Blend::BlendFactor, Blend::Zero,
                                                Blend::One, Blend::Zero, Color(255, 0, 0, 255)));
            // The BlendState set above already pushed RED. Draw left, then change ONLY BlendFactor.
            drawQuad(dev, *leftQuadVb_);                       // RED constant
            dev.setBlendFactorProperty(Color(0, 255, 0, 255));
            drawQuad(dev, *midQuadVb_);                        // GREEN constant
            dev.setBlendFactorProperty(Color(0, 0, 255, 255));
            drawQuad(dev, *rightQuadVb_);                      // BLUE constant
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color l = readAt(10, 24), m = readAt(32, 24), r = readAt(53, 24);
            check(isRed(l),   "3-BlendFactor: left  (10,24)", l, "RED");
            check(isGreen(m), "3-BlendFactor: mid   (32,24)", m, "GREEN");
            check(isBlue(r),  "3-BlendFactor: right (53,24)", r, "BLUE");
            ++frame_;
            return;
        }

        if (frame_ == 5)
        {
            // SpriteBatch path: white texture through a BlendFactor blend state => center = constant.
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color(0, 0, 0, 255));
            sb_->Begin(SpriteSortMode::Deferred,
                       makeBlend(Blend::BlendFactor, Blend::Zero, Blend::One, Blend::Zero,
                                 Color(kBFr, kBFg, kBFb, kBFa)));
            sb_->Draw(*whiteTex_, Rectangle(0, 0, kRTW, kRTH), Rectangle(0, 0, 2, 2), Color::White);
            sb_->End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color c = readAt(kRTW / 2, kRTH / 2);
            check(isRGB(c, kBFr, kBFg, kBFb), "SpriteBatch BlendFactor (white tex*constant)", c, "~(64,128,192)");

            std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
            Exit();
        }
    }

public:
    SdlGpuRenderTargetBlendFactorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SdlGpuRenderTargetBlendFactorTest game;
    game.Run();
    return game.getResult();
}
