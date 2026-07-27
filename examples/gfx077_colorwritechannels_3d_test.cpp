// SPDX-License-Identifier: MS-PL
// REMED-GFX-077 runtime verification (generic 3D path, backend-agnostic).
//
// Verifies that BlendState.ColorWriteChannels (RT0 per-channel colour write mask) and
// BlendState.MultiSampleMask are honoured by the backend selected at compile time, now that both
// are plumbed through IGraphicsBackend::ApplyBlendState (REMED-GFX-077). Unlike the canonical
// *_colorwritechannels_test.cpp harnesses (which drive SpriteBatch), this test draws a full-screen
// coloured quad through the generic 3D pipeline (DrawPrimitives + BasicEffect) into an off-screen
// RenderTarget2D and reads it back with GetData(). That is deliberate: on some backends (notably
// WebGPU) the fixed internal SpriteBatch pipeline does NOT consume ColorWriteChannels, so only the
// keyed 3D pipeline path actually exercises the GFX-077 fix. The 3D path is common to every backend
// this harness is compiled for (SDL_GPU / WEBGPU / D3D11 / D3D9), so one scene verifies them all.
//
// Isolation strategy: the quad is drawn with Opaque blend (SrcBlend=One, DstBlend=Zero -> blend
// disabled, output == source), so the ONLY thing that can preserve a destination channel is the
// colour write mask gating the write. Clear the RT to a known destination colour D, draw the quad
// with source colour S, then a channel that is masked OUT must still read the destination while a
// channel that is masked IN reads the source.
//
// The assertion is DIFFERENTIAL, not absolute: two baseline renders establish, per channel, the
// "nothing written" value (ColorWriteChannels.None -> dst) and the "everything written" value
// (ColorWriteChannels.All -> src). Every other mask is then checked channel-by-channel against
// those two baselines (masked-in channel == src, masked-out channel == dst). This is invariant to
// any monotonic colour transform the backend applies on the render-target path -- e.g. WebGPU
// resolves SurfaceFormat.Color to an sRGB wgpu format, so its stored RGB is gamma-encoded (10->56,
// 200->229) while alpha stays linear; an absolute (200,20,30,40)-style expectation would spuriously
// fail there even though the write mask is perfect. What GFX-077 must guarantee is that the mask
// gates exactly the right channels, which the differential check verifies directly and portably.
//
//   D (clear/destination) = (10, 20, 30, 40)   [as stored/read: backend colour space may transform]
//   S (quad source)       = (200, 100, 50, 220)
//
// A(Red)->B(Green)->A(Red) within one frame proves each draw selects its own pipeline/blend state
// (no stale keyed pipeline / no last-wins).
//
// When GFX077_MULTISAMPLEMASK_SUPPORTED is defined (backends whose native API exposes a functional
// sample coverage mask), a 4x MSAA RenderTarget is used to prove MultiSampleMask=0 discards all
// coverage (resolves to the clear colour) while the default mask renders normally. This block is
// wrapped so a runtime that cannot create/resolve an MSAA RT reports a NOTE rather than crashing.
//
// Exit code 0 = every executed check PASSED, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kBBW = 96, kBBH = 72;   // backbuffer (asymmetric vs RT to catch dimension leaks)
constexpr int kRtW = 64, kRtH = 64;   // off-screen render target
}

class Gfx077ColorWriteChannels3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D>        rt_;
    std::unique_ptr<VertexBuffer>          quadVb_;
    std::unique_ptr<BasicEffect>           fx_;
    int frame_ = 0, pass_ = 0, fail_ = 0;
    Color dst_{0, 0, 0, 0};   // per-channel "nothing written" baseline (ColorWriteChannels.None)
    Color src_{0, 0, 0, 0};   // per-channel "everything written" baseline (ColorWriteChannels.All)

    static const Color D() { return Color(10, 20, 30, 40); }   // destination / clear
    static const Color S() { return Color(200, 100, 50, 220); } // source / quad colour

    // XNA ColorWriteChannels bits: R=1, G=2, B=4, A=8.
    static constexpr int kR = 1, kG = 2, kB = 4, kA = 8;

    static std::string Str(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }
    static bool Eq(const Color& c, const Color& e)
    {
        return c.getRProperty() == e.getRProperty() && c.getGProperty() == e.getGProperty() &&
               c.getBProperty() == e.getBProperty() && c.getAProperty() == e.getAProperty();
    }
    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

    // Per-channel expectation for a given ColorWriteChannels mask: masked-in channel takes the src
    // baseline, masked-out channel keeps the dst baseline.
    Color Expected(int maskBits) const
    {
        return Color(
            static_cast<int>((maskBits & kR) ? src_.getRProperty() : dst_.getRProperty()),
            static_cast<int>((maskBits & kG) ? src_.getGProperty() : dst_.getGProperty()),
            static_cast<int>((maskBits & kB) ? src_.getBProperty() : dst_.getBProperty()),
            static_cast<int>((maskBits & kA) ? src_.getAProperty() : dst_.getAProperty()));
    }

    static BlendState MakeBlend(ColorWriteChannels cw, unsigned int sampleMask = 0xFFFFFFFFu)
    {
        BlendState bs; // default = Opaque (One/Zero) -> blend disabled, output == source
        bs.setColorWriteChannelsProperty(cw);
        bs.setMultiSampleMaskProperty(static_cast<int>(sampleMask));
        return bs;
    }

    static std::unique_ptr<VertexBuffer> MakeQuad(GraphicsDevice& dev, const Color& col)
    {
        // Full-NDC quad ([-1,1]^2), flat vertex colour = the blend "source".
        const VertexPositionColor q[6] = {
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3(-1.f, -1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
            { Vector3(-1.f,  1.f, 0.f), col },
            { Vector3( 1.f, -1.f, 0.f), col },
            { Vector3( 1.f,  1.f, 0.f), col },
        };
        auto vb = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(),
                                                 6, BufferUsage::None);
        vb->SetData(q, 0, 6);
        return vb;
    }

    void DrawQuad(GraphicsDevice& dev)
    {
        fx_->Apply();
        dev.SetVertexBuffer(quadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    Color RenderCenter(GraphicsDevice& dev, RenderTarget2D* target, const BlendState& blend)
    {
        dev.SetRenderTarget(target);
        dev.Clear(D());
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(blend);
        DrawQuad(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // Full-texture readback (rect==nullptr, elementCount==total): the portable path that hits
        // every backend's RenderTarget2D GPU-readback fallback. This comment previously recorded
        // that a 1x1-rect read "reads back all zeros on D3D11/D3D9" -- that was the REMED-GFX-127
        // fabrication, not a property of the rectangle overload: the rectangle path does reach the
        // backend, it simply had no readback to reach on those backends. Both now return real
        // content; the whole-level read is kept here because it is what this GFX-077 scene asserts.
        std::vector<Color> pix(static_cast<std::size_t>(kRtW) * kRtH, Color(0, 0, 0, 0));
        target->GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix[static_cast<std::size_t>(kRtH / 2) * kRtW + kRtW / 2];
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRtW, kRtH, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        quadVb_ = MakeQuad(dev, S());
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->VertexColorEnabled = true;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // Frame 0: establish the per-channel dst/src baselines and prove they discriminate (every
        // channel differs), so the differential mask checks below are meaningful.
        if (frame_ == 0)
        {
            dst_ = RenderCenter(dev, rt_.get(), MakeBlend(ColorWriteChannels::None));
            src_ = RenderCenter(dev, rt_.get(), MakeBlend(ColorWriteChannels::All));
            const bool discriminates =
                dst_.getRProperty() != src_.getRProperty() && dst_.getGProperty() != src_.getGProperty() &&
                dst_.getBProperty() != src_.getBProperty() && dst_.getAProperty() != src_.getAProperty();
            check(discriminates,
                  "baselines discriminate all 4 channels: None(dst)=" + Str(dst_) + " All(src)=" + Str(src_));
            ++frame_;
            return;
        }

        struct Case { ColorWriteChannels cw; int bits; const char* name; };
        static const Case kCases[] = {
            { ColorWriteChannels::Red,                            kR,      "Red" },
            { ColorWriteChannels::Green,                          kG,      "Green" },
            { ColorWriteChannels::Blue,                           kB,      "Blue" },
            { ColorWriteChannels::Alpha,                          kA,      "Alpha" },
            { ColorWriteChannels::Red | ColorWriteChannels::Blue, kR | kB, "Red|Blue" },
        };
        constexpr int kNumCases = static_cast<int>(sizeof(kCases) / sizeof(kCases[0]));

        if (frame_ <= kNumCases)
        {
            const Case& c = kCases[frame_ - 1];
            const Color px = RenderCenter(dev, rt_.get(), MakeBlend(c.cw));
            const Color ex = Expected(c.bits);
            check(Eq(px, ex), std::string("ColorWriteChannels.") + c.name +
                  ": masked-in==src, masked-out==dst -> expected " + Str(ex) + ", got " + Str(px));
            ++frame_;
            return;
        }

        if (frame_ == kNumCases + 1)
        {
            // A(Red) -> B(Green) -> A(Red) in one frame: each draw must pick its own pipeline/state.
            const Color a1 = RenderCenter(dev, rt_.get(), MakeBlend(ColorWriteChannels::Red));
            const Color b  = RenderCenter(dev, rt_.get(), MakeBlend(ColorWriteChannels::Green));
            const Color a2 = RenderCenter(dev, rt_.get(), MakeBlend(ColorWriteChannels::Red));
            check(Eq(a1, Expected(kR)) && Eq(b, Expected(kG)) && Eq(a2, Expected(kR)),
                  "A(Red)->B(Green)->A(Red): each draw selects its own write mask (no stale/last-wins): "
                  "A1=" + Str(a1) + " B=" + Str(b) + " A2=" + Str(a2));
            ++frame_;
            return;
        }

#ifdef GFX077_MULTISAMPLEMASK_SUPPORTED
        if (frame_ == kNumCases + 2)
        {
            try
            {
                auto msaa = std::make_unique<RenderTarget2D>(
                    dev, kRtW, kRtH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                    RenderTargetUsage::DiscardContents);
                // Baselines on the MSAA target itself (its resolve colour space may differ from rt_).
                const Color mAll = RenderCenter(dev, msaa.get(), MakeBlend(ColorWriteChannels::All, 0xFFFFFFFFu));
                const Color m0   = RenderCenter(dev, msaa.get(), MakeBlend(ColorWriteChannels::All, 0u));
                check(Eq(mAll, src_),
                      "MSAA MultiSampleMask=all -> normal full-coverage resolve to src: " + Str(mAll));
                check(Eq(m0, dst_),
                      "MSAA MultiSampleMask=0 -> no coverage written, resolves to clear dst: " + Str(m0));
            }
            catch (const std::exception& e)
            {
                std::printf("[NOTE] MSAA MultiSampleMask sub-test skipped (RT unavailable in this "
                            "runtime): %s\n", e.what());
                std::fflush(stdout);
            }
            ++frame_;
            return;
        }
#endif

        std::printf("\n=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    Gfx077ColorWriteChannels3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }
    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    Gfx077ColorWriteChannels3DTest game;
    game.Run();
    return game.getResult();
}
