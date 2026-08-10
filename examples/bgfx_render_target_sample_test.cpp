// SPDX-License-Identifier: MS-PL
// Task 333: RenderTarget2D sampled as Texture2D after unbinding, on Bgfx.
//
// This file's original premise -- "Bgfx has no usable GPU pixel-readback path in this project, so
// this cannot be pixel-verified" -- has been false for a long time: bgfx_rendertarget_orientation_
// test.cpp, bgfx_rendertarget_viewport_test.cpp and others all pixel-verify this very path through
// GetBackBufferData. Until REMED-GFX-155 the file still only printed "did not crash" and `main`
// returned 0 unconditionally, so it asserted nothing at all.
//
// That mattered, because the sequence it performs IS REMED-GFX-155's sequence exactly: render into
// a RenderTarget2D, unbind it, and draw it through SpriteBatch onto the BACKBUFFER in the same
// public frame. bgfx executed the backbuffer's view (id 0) before the render target's (id >= 1),
// so the consumer sampled an image nothing had rendered into and the backbuffer came back entirely
// empty -- and this test, the one named for exactly this scenario, passed throughout. It is the
// clearest false positive REMED-GFX-155's audit found, and it now reads the result.
//
// It keeps its original subject too (REMED-GFX-078 / Task 873: SpriteBatch must not reach a
// RenderTarget2D's renderer through a static_cast to the unrelated sibling BgfxTextureRenderer --
// undefined behaviour that corrupted the draw). A crash is still a failure; producing nothing is
// now also a failure.
//
// The render target is deliberately built from TWO distinct non-black colours, so an empty result,
// a stale buffer, a uniform fill and a single-colour-only result each fail distinguishably. The
// assertion is on the SET of colours and their pixel counts rather than on where each one landed:
// sampling orientation is REMED-GFX-067's contract and is owned by
// rendertarget_sampling_orientation_test.cpp and bgfx_rendertarget_orientation_test.cpp, so this
// file must not double-declare it; REMED-GFX-153's partial-source coverage lives in its dedicated
// source_rectangle_orientation_test.cpp fixture.
//
// Exit code 0 = the unbound render target reached the backbuffer with its real content.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 64;

    /// The render target's upper band. Mid-tone, no channel at 0 or 255, so neither an empty image
    /// nor a saturated one can coincide with it.
    const Color kBandA(40, 190, 90, 255);
    /// The render target's lower band, differing in every channel from kBandA.
    const Color kBandB(200, 60, 145, 255);

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }
}

class BgfxRenderTargetSampleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>    sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    Texture2D bandB_;                    ///< 1x1 kBandB, the source of the render target's lower band.
    int  frame_  = 0;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize);
        bandB_ = Texture2D(device, 1, 1);
        bandB_.SetData(&kBandB, 1);
    }

    void Draw(const GameTime&) override
    {
        if (frame_ > 0) return;
        ++frame_;

        auto& device = getGraphicsDeviceProperty();

        // --- Pass 1: build a two-band render target, then unbind. NEVER read it back: reading the
        // source is the accidental execution barrier REMED-GFX-155 was hiding behind. ---
        device.SetRenderTarget(rt_.get());
        device.Clear(kBandA);
        {
            SpriteBatch band(device);
            SamplerState point = SamplerState::PointClamp;
            band.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            band.Draw(bandB_, Rectangle(0, kRTSize / 2, kRTSize, kRTSize / 2),
                      Rectangle(0, 0, 1, 1), Color::White);
            band.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Pass 2: sample the unbound RT as a Texture2D via SpriteBatch, onto the BACKBUFFER, in
        // this same public frame. No Present, no GetData of the source, no extra frame. ---
        device.Clear(Color(0, 0, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        sb_->Draw(*rt_,
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        check(true, "Bgfx RenderTarget2D-as-Texture2D SpriteBatch::Draw did not crash "
                    "(REMED-GFX-078 / Task 873: no static_cast to an unrelated sibling renderer)");

        std::vector<Color> frame(static_cast<std::size_t>(kRTSize) * kRTSize,
                                 Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool threw = false;
        std::string what;
        try { device.GetBackBufferData(frame.data(), 0, static_cast<int>(frame.size())); }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        if (threw)
        {
            check(false, std::string("the backbuffer must be readable so this test can assert "
                                     "something -- got ") + what);
        }
        else
        {
            int a = 0, b = 0, empty = 0, poison = 0, other = 0;
            Color firstOther(0, 0, 0, 0);
            for (const Color& c : frame)
            {
                if (Same(c, kBandA)) { ++a; continue; }
                if (Same(c, kBandB)) { ++b; continue; }
                if (c.getRProperty() == 0 && c.getGProperty() == 0 &&
                    c.getBProperty() == 0 && c.getAProperty() == 0) { ++empty; continue; }
                if (c.getRProperty() == 0xCD && c.getGProperty() == 0xCD &&
                    c.getBProperty() == 0xCD && c.getAProperty() == 0xCD) { ++poison; continue; }
                if (other == 0) firstOther = c;
                ++other;
            }
            const int half = kRTSize * kRTSize / 2;
            std::printf("[INFO] backbuffer: %d texels of %s, %d of %s, %d entirely empty, "
                        "%d never written, %d other (first %s)\n",
                        a, ColorText(kBandA).c_str(), b, ColorText(kBandB).c_str(),
                        empty, poison, other, ColorText(firstOther).c_str());
            std::fflush(stdout);

            // The band positions are deliberately NOT asserted -- sampling orientation belongs to
            // REMED-GFX-067's own fixtures. What is asserted is that the producer's real content
            // reached the backbuffer, which is what REMED-GFX-155 broke and what this file, named
            // for exactly this sequence, previously could not notice.
            check(empty == 0, "no backbuffer texel is entirely empty -- an unbound render target "
                              "sampled on the BACKBUFFER in the same public frame must carry its "
                              "producer's content (REMED-GFX-155)" +
                              (empty ? " [" + std::to_string(empty) + " empty]" : ""));
            check(poison == 0, "every backbuffer texel was written" +
                               (poison ? " [" + std::to_string(poison) + " untouched]" : ""));
            check(a == half && b == half,
                  "both of the producer's bands reached the backbuffer, each covering exactly half "
                  "of it (" + std::to_string(a) + " + " + std::to_string(b) + " of " +
                  std::to_string(half) + " + " + std::to_string(half) + ")");
            check(other == 0, "no unexpected colour appeared" +
                              (other ? " [" + std::to_string(other) + ", first " +
                                       ColorText(firstOther) + "]" : ""));
        }

        std::printf("[INFO] BGFX RenderTarget2D sample-after-unbind: %d/%d checks passed\n",
                    passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    BgfxRenderTargetSampleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    BgfxRenderTargetSampleTest game;
    game.Run();
    return game.Result();
}
