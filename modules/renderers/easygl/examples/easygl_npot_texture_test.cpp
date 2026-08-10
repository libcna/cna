// SPDX-License-Identifier: MS-PL
// Task 268: EasyGL integration test — non-power-of-two (NPOT) texture pixel readback.
//
// Uploads a 3x5 NPOT texture with a distinct solid colour per row, draws it
// full-screen via SpriteBatch, and reads back a pixel from each row's screen-space
// band to confirm NPOT upload + GPU sampling works correctly end-to-end (not just
// the CPU-side SetData/GetData round trip already covered by Texture2DTests.cpp's
// LevelCount tests). 3 is deliberately not a multiple of 4, exercising GL row
// alignment for a non-power-of-two width. Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTexW = 3;
    constexpr int kTexH = 5;

    const Color kRowColors[kTexH] = {
        Color(255, 0,   0,   255), // row 0: red
        Color(0,   255, 0,   255), // row 1: green
        Color(0,   0,   255, 255), // row 2: blue
        Color(255, 255, 0,   255), // row 3: yellow
        Color(255, 0,   255, 255), // row 4: magenta
    };
}

class NpotTextureTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    tex_;
    bool                         done_   = false;
    int                          result_ = 1; // 1 = fail until proven otherwise

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        std::vector<std::uint8_t> px(static_cast<std::size_t>(kTexW) * kTexH * 4);
        for (int y = 0; y < kTexH; ++y)
        {
            const Color& c = kRowColors[y];
            for (int x = 0; x < kTexW; ++x)
            {
                std::uint8_t* p = &px[(static_cast<std::size_t>(y) * kTexW + x) * 4];
                p[0] = c.getRProperty();
                p[1] = c.getGProperty();
                p[2] = c.getBProperty();
                p[3] = c.getAProperty();
            }
        }
        tex_ = Texture2D::CreateFromPixels(device, kTexW, kTexH, px);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);

        // Point filtering keeps row boundaries crisp (no bilinear blending between rows).
        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr);
        sb_->Draw(tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, kTexW, kTexH), Color::White);
        sb_->End();

        bool pass = true;
        for (int y = 0; y < kTexH; ++y)
        {
            // Sample the vertical centre of each texture row's screen-space band.
            const int sy = (2 * y + 1) * H / (2 * kTexH);
            const Rectangle region(W / 2, sy, 1, 1);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&region, &pixel, 0, 1);

            const Color& expected = kRowColors[y];
            const bool rowPass = (pixel.getRProperty() == expected.getRProperty() &&
                                  pixel.getGProperty() == expected.getGProperty() &&
                                  pixel.getBProperty() == expected.getBProperty());
            std::printf("[%s] NPOT 3x5 row %d: got=(%d,%d,%d) want=(%d,%d,%d)\n",
                        rowPass ? "PASS" : "FAIL", y,
                        pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(),
                        expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
            pass = pass && rowPass;
        }

        result_ = pass ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    NpotTextureTest game;
    game.Run();
    return game.getResult();
}
