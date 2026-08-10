// SPDX-License-Identifier: MS-PL
// REMED-GFX-074: Vulkan defers every draw into a RenderTarget2D to a single Present-time record.
// Two consequences this test pins:
//
//   (1) VISIBILITY -- RenderTarget2D::GetData() called after SpriteBatch.End()/unbind but BEFORE
//       Present() must return the RENDERED content, not the cleared target. Pre-fix Vulkan had no
//       RenderTarget2D readback at all (the base ITextureRenderer::GetData no-op), so it returned
//       all-zeros, and even conceptually the deferred sprite batch was never flushed into the RT.
//
//   (2) LIFETIME -- destroying a RenderTarget2D after queuing work into it but before Present()
//       must NOT leave a dangling target pointer in the deferred queues (activeBatches_/pending3D_/
//       clearedRTs_). Pre-fix this dangled and the next Present dereferenced freed memory (UAF /
//       SIGSEGV). Run under ASan for the memory-safety proof.
//
// Also covers: multiple batches into one RT (compositing/order), multiple independent RTs, a mixed
// 3D + 2D sequence, target rebind, and a "content survives a subsequent Present" check that a
// consumed batch is not replayed a second time (would re-clear the target to black).
//
// Exit code 0 = all checks PASS, 1 = any FAIL. Headless (SDL dummy/Xvfb).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kW = 64;
    constexpr int kH = 48;

    bool IsRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsBlue(const Color& c)  { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }
    // REMED-GFX-127 false-positive audit: this predicate used to ignore alpha entirely, so a pixel
    // GetData never wrote -- the fabricated transparent black the shared layer produced from a
    // renderer with no readback -- satisfied it just as well as the opaque Color::Black clear it is
    // meant to recognise. Every clear this file performs is opaque, so requiring alpha 255 costs
    // nothing and makes "an unwritten readback" fail here.
    bool IsBlack(const Color& c) { return c.getRProperty() < 40 && c.getGProperty() < 40 && c.getBProperty() < 40
                                          && c.getAProperty() == 255; }
    bool IsFabricated(const Color& c) { return c.getRProperty() == 0 && c.getGProperty() == 0 &&
                                               c.getBProperty() == 0 && c.getAProperty() == 0; }
}

class GetDataLifetimeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int  pass_ = 0;
    int  total_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    std::unique_ptr<RenderTarget2D> makeRt()
    {
        auto& dev = getGraphicsDeviceProperty();
        return std::make_unique<RenderTarget2D>(dev, kW, kH, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void drawSprite(const Rectangle& dst, const Color& tint)
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, dst, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    std::vector<Color> readRt(RenderTarget2D& rt)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kW) * kH, Color(0, 0, 0, 0));
        rt.GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    const Color& at(const std::vector<Color>& pix, int x, int y)
    {
        return pix[static_cast<std::size_t>(y) * kW + x];
    }

    template <typename Pred>
    int countIf(const std::vector<Color>& pix, Pred pred)
    {
        int n = 0;
        for (const auto& c : pix) if (pred(c)) ++n;
        return n;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        // --- Scene 1: GetData BEFORE Present observes the sprite (visibility). --------------
        {
            auto rt = makeRt();
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(10, 8, 20, 16), Color::Red);   // a red block in the RT
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const std::vector<Color> pix = readRt(*rt);         // <-- BEFORE any Present()
            const int redN = countIf(pix, IsRed);
            // Multiple spatial probes: inside the block is red, well outside is the black clear.
            check(IsRed(at(pix, 20, 16)), "S1a center of red block is RED before Present");
            check(IsRed(at(pix, 11, 9)),  "S1b top-left of red block is RED before Present");
            check(IsBlack(at(pix, 2, 2)), "S1c far corner is the BLACK clear before Present");
            check(redN >= 250 && redN <= 400,
                  "S1d red pixel count ~= 20x16 (=320): " + std::to_string(redN));
            // REMED-GFX-127: no part of a readback may be content GetData never actually read. An
            // opaque clear plus an opaque sprite leaves no transparent-black pixel anywhere, so a
            // single one means fabricated bytes reached the caller.
            check(countIf(pix, IsFabricated) == 0,
                  "S1e not one pixel of the readback is fabricated transparent black");
        }

        // --- Scene 2: two batches into one RT, both visible before Present (compositing). ----
        {
            auto rt = makeRt();
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(2, 2, 16, 16), Color::Red);
            drawSprite(Rectangle(40, 24, 16, 16), Color::Blue);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const std::vector<Color> pix = readRt(*rt);
            check(IsRed(at(pix, 8, 8)),    "S2a first batch (red) visible before Present");
            check(IsBlue(at(pix, 47, 31)), "S2b second batch (blue) visible before Present");
        }

        // --- Scene 3: two independent RTs, each GetData sees ITS OWN content. ----------------
        {
            auto rtA = makeRt();
            auto rtB = makeRt();
            dev.SetRenderTarget(rtA.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(0, 0, kW, kH), Color::Red);
            dev.SetRenderTarget(rtB.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(0, 0, kW, kH), Color(0, 255, 0, 255));  // pure green (XNA Color::Green is 0,128,0)
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const std::vector<Color> a = readRt(*rtA);
            const std::vector<Color> b = readRt(*rtB);
            check(countIf(a, IsRed) >= kW * kH / 2 && countIf(a, IsGreen) == 0,
                  "S3a RT-A is RED (not RT-B's green)");
            check(countIf(b, IsGreen) >= kW * kH / 2 && countIf(b, IsRed) == 0,
                  "S3b RT-B is GREEN (not RT-A's red)");
        }

        // --- Scene 4: content survives a later Present (consumed batch not replayed). --------
        // If the flushed batch/clear were NOT consumed, the real Present would re-run the RT pass
        // (re-clear to black), so the second read would be black instead of red.
        {
            auto rt = makeRt();
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(0, 0, kW, kH), Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const std::vector<Color> before = readRt(*rt);  // flush + consume
            check(countIf(before, IsRed) >= kW * kH / 2, "S4a RT is red after first GetData");

            dev.Clear(Color::CornflowerBlue);               // backbuffer only; no RT work queued
            dev.Present();                                      // must NOT replay the consumed RT batch

            const std::vector<Color> after = readRt(*rt);   // no pending work -> reads stored image
            check(countIf(after, IsRed) >= kW * kH / 2,
                  "S4b RT still red after a subsequent Present (no double-replay / re-clear)");
        }

        // --- Scene 5: mixed 3D + 2D into one RT, both flushed & visible before Present. -------
        // Non-overlapping regions: the deferred renderer replays all sprite batches before all 3D
        // draws within one target's pass (a pre-existing 2D-before-3D characteristic, unchanged by
        // this fix), so this asserts BOTH classes of work reach the readback, not their z-order.
        // The 3D quad fills the LEFT half (NDC x in [-1,0]); the 2D sprite sits on the RIGHT half.
        {
            auto rt = makeRt();
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            {
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.VertexColorEnabled = true;
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                const Color g(0, 255, 0, 255);
                const VertexPositionColor quad[6] = {   // left half of NDC
                    { Vector3(-1.f,  1.f, 0.f), g },
                    { Vector3(-1.f, -1.f, 0.f), g },
                    { Vector3( 0.f, -1.f, 0.f), g },
                    { Vector3(-1.f,  1.f, 0.f), g },
                    { Vector3( 0.f, -1.f, 0.f), g },
                    { Vector3( 0.f,  1.f, 0.f), g },
                };
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            }
            drawSprite(Rectangle(44, 12, 16, 24), Color::Red);   // 2D on the right half
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const std::vector<Color> pix = readRt(*rt);
            check(countIf(pix, IsGreen) > 200, "S5a 3D green fill (left half) visible before Present");
            check(IsRed(at(pix, 50, 24)),      "S5b 2D red sprite (right half) visible before Present");
        }

        // --- Scene 6: DESTROY the RT after queuing work, before Present (lifetime / UAF). -----
        // Pre-fix: dangling target pointer in the deferred queues -> UAF at the Present below.
        {
            auto rt = makeRt();
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(4, 4, 20, 20), Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            rt.reset();                         // destroy while its sprite batch is still queued
            dev.Clear(Color::CornflowerBlue);
            dev.Present();                          // must not dereference the freed target
            check(true, "S6 destroy-before-Present did not crash (no dangling target pointer)");
        }

        // --- Scene 6b: the frame AFTER Scene 6 must still render exactly (REMED-GFX-166). -------
        // Scene 6 asserts only "did not crash", which is all it CAN assert about a target nobody
        // reads -- and REMED-GFX-166 is precisely a silent, non-crashing wrong result, so that shape
        // of check could never have caught it. The observable version of Scene 6 (a destroyed target
        // that another queued draw SAMPLES) lives in examples/deferred_source_lifetime_test.cpp.
        // What is added here is the part this fixture owns: Scene 6 changed how a dying target
        // interacts with the pending queues, so the very next target must still be correct -- which
        // catches a lifetime fix that leaks a stale entry, reorders the queue or drops the wrong one.
        {
            auto rt = makeRt();
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSprite(Rectangle(0, 0, kW, kH), Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pix = readRt(*rt);
            check(countIf(pix, IsRed) >= kW * kH / 2 && countIf(pix, IsFabricated) == 0,
                  "S6b the render target created right after a destroy-before-Present is exact");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = (pass_ == total_) ? 0 : 1;
        Exit();
    }

public:
    GetDataLifetimeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(96);
        gdm_->setPreferredBackBufferHeightProperty(72);
    }

    int getResult() const { return result_; }
};

int main()
{
    GetDataLifetimeTest game;
    game.Run();
    return game.getResult();
}
