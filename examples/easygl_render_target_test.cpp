// SPDX-License-Identifier: MS-PL
// Task 87: EasyGL integration test — RenderTarget2D pixel readback.
//
// Renders a solid-green clear into a 64×64 RenderTarget2D, then uses the RT
// as a texture to fill the default framebuffer, reads back the centre pixel,
// and asserts G=255, R=0, B=0.
//
// REMED-GFX-147 strengthened this file. Its original single check — a SOLID clear colour sampled
// at the CENTRE pixel — exercised precisely the defective path (a RenderTarget2D used as a
// SpriteBatch texture) and could not fail for any orientation, because a uniform image is
// identical under every flip and the centre pixel is a fixed point of a vertical mirror. It
// passed unchanged while EasyGL sampled every render target upside down. A second phase now
// renders four DIFFERENT quadrant colours into the same target and asserts where each one lands.
// Exit code 0 = PASS, 1 = FAIL.

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

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

class RenderTargetTest : public Game
{
    std::unique_ptr<SpriteBatch>   sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    Texture2D                       white_;   ///< 1x1 opaque white, tinted to fill quadrants.
    bool                            done_ = false;
    int                             result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize);
        white_ = Texture2D(device, 1, 1);
        const Color white = Color::White;
        white_.SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // --- Pass 1: render solid green into the RT ---
        device.SetRenderTarget(rt_.get());
        device.SetDepthTestEnabled(false);
        device.Clear(Color(0, 255, 0, 255));
        device.SetRenderTarget(nullptr);

        // --- Pass 2: blit RT as a full-screen texture ---
        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);

        sb_->Begin();
        sb_->Draw(*rt_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        // Read back the centre pixel from the default framebuffer.
        const Rectangle region(W / 2, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);

        const bool pass = (pixel.getRProperty() == 0   &&
                           pixel.getGProperty() == 255 &&
                           pixel.getBProperty() == 0);

        if (pass)
        {
            std::printf("[PASS] RenderTarget2D: pixel=(%d,%d,%d,%d)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
        }
        else
        {
            std::printf("[FAIL] RenderTarget2D: pixel=(%d,%d,%d,%d), expected (0,255,0,*)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
        }

        // --- REMED-GFX-147: the same blit, with an image that is not the same under a flip ---
        const Color topLeft(255, 0, 0, 255), topRight(0, 0, 255, 255);
        const Color bottomLeft(255, 255, 0, 255), bottomRight(255, 0, 255, 255);
        const int half = kRTSize / 2;

        device.SetRenderTarget(rt_.get());
        device.SetDepthTestEnabled(false);
        device.Clear(Color(0, 0, 0, 255));
        {
            SamplerState point = SamplerState::PointClamp;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(white_, Rectangle(0, 0, half, half),       Rectangle(0, 0, 1, 1), topLeft);
            sb_->Draw(white_, Rectangle(half, 0, half, half),    Rectangle(0, 0, 1, 1), topRight);
            sb_->Draw(white_, Rectangle(0, half, half, half),    Rectangle(0, 0, 1, 1), bottomLeft);
            sb_->Draw(white_, Rectangle(half, half, half, half), Rectangle(0, 0, 1, 1), bottomRight);
            sb_->End();
        }
        device.SetRenderTarget(nullptr);

        device.Clear(Color(0, 0, 0, 255));
        {
            SamplerState point = SamplerState::PointClamp;
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(*rt_, Rectangle(0, 0, W, H), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->End();
        }

        auto readAt = [&](int x, int y) {
            const Rectangle r(x, y, 1, 1);
            Color c(0, 0, 0, 0);
            device.GetBackBufferData(&r, &c, 0, 1);
            return c;
        };
        auto same = [](const Color& a, const Color& b) {
            return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
                   a.getBProperty() == b.getBProperty();
        };
        const Color gotTL = readAt(W / 4, H / 4),         gotTR = readAt(W * 3 / 4, H / 4);
        const Color gotBL = readAt(W / 4, H * 3 / 4),     gotBR = readAt(W * 3 / 4, H * 3 / 4);
        const bool quadrantsOk = same(gotTL, topLeft) && same(gotTR, topRight) &&
                                 same(gotBL, bottomLeft) && same(gotBR, bottomRight);
        const bool verticallyMirrored = same(gotTL, bottomLeft) && same(gotTR, bottomRight) &&
                                        same(gotBL, topLeft) && same(gotBR, topRight);
        std::printf("[%s] RenderTarget2D sampled as a texture keeps its quadrants: "
                    "TL=(%d,%d,%d) TR=(%d,%d,%d) BL=(%d,%d,%d) BR=(%d,%d,%d)%s\n",
                    quadrantsOk ? "PASS" : "FAIL",
                    gotTL.getRProperty(), gotTL.getGProperty(), gotTL.getBProperty(),
                    gotTR.getRProperty(), gotTR.getGProperty(), gotTR.getBProperty(),
                    gotBL.getRProperty(), gotBL.getGProperty(), gotBL.getBProperty(),
                    gotBR.getRProperty(), gotBR.getGProperty(), gotBR.getBProperty(),
                    (!quadrantsOk && verticallyMirrored)
                        ? " -- EXACTLY VERTICALLY MIRRORED (REMED-GFX-147)" : "");

        result_ = (pass && quadrantsOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    RenderTargetTest game;
    game.Run();
    return game.getResult();
}
