// SPDX-License-Identifier: MS-PL
// Task 821: verify non-power-of-two (NPOT) texture upload + GPU sampling works correctly on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_npot_texture_test.cpp (Task 268, already reused
// verbatim on Vulkan). Not a verbatim reuse: that file calls GraphicsDevice::SetDepthTestEnabled
// (false), which throws on Bgfx ("SetDepthTestEnabled / SetBlend* are not yet wired into bgfx
// state flags" -- a pre-existing, deliberate stub), replaced with the equivalent
// DepthStencilState-based call, matching Tasks 752-755's established substitution; and reads back
// 5 rows from ONE rendered frame, incompatible with Bgfx's own "first GetBackBufferData read per
// rendered frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per
// row, mirroring Tasks 759-819's established Bgfx pattern.
//
// Uploads a 3x5 NPOT texture (3 is deliberately not a multiple of 4, exercising row-alignment
// handling for a non-power-of-two width) with a distinct solid colour per row, draws it
// full-screen via SpriteBatch with point filtering, and reads back a pixel from each row's
// screen-space band.
//
// Exit code 0 = all 5 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
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

static constexpr int kSize = 64;

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

class BgfxNpotTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D tex_;
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, int row)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);

            const auto& vp = dev.getViewportProperty();
            const int W = vp.getWidthProperty();
            const int H = vp.getHeightProperty();

            // Point filtering keeps row boundaries crisp (no bilinear blending between rows).
            sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                       const_cast<SamplerState*>(&SamplerState::PointClamp),
                       nullptr, nullptr);
            sb_->Draw(tex_, Rectangle(0, 0, W, H), Rectangle(0, 0, kTexW, kTexH), Color::White);
            sb_->End();

            const int sy = (2 * row + 1) * H / (2 * kTexH);
            const Rectangle region(W / 2, sy, 1, 1);
            dev.GetBackBufferData(&region, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

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

        auto& dev = getGraphicsDeviceProperty();

        int passCount = 0;
        const int totalChecks = kTexH;

        for (int y = 0; y < kTexH; ++y)
        {
            const Color got = RunCheck(dev, y);
            const Color& expected = kRowColors[y];
            const bool ok = (got.getRProperty() == expected.getRProperty() &&
                             got.getGProperty() == expected.getGProperty() &&
                             got.getBProperty() == expected.getBProperty());
            std::printf("[%s] NPOT 3x5 row %d: got=(%d,%d,%d) want=(%d,%d,%d)\n",
                        ok ? "PASS" : "FAIL", y,
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
            if (ok) ++passCount;
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
        result_ = (passCount == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    BgfxNpotTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxNpotTextureTest game;
    game.Run();
    return game.getResult();
}
