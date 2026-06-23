// SPDX-License-Identifier: MS-PL
// Task 171: Texture2D mip-level SetData / GetData round-trip.
//
// A 4×4 mipMap texture has 3 mip levels: 4×4 (mip 0), 2×2 (mip 1), 1×1 (mip 2).
//
// Test plan:
//   SetData(0, nullptr, red16,   0, 16)  → mip 0: all 16 pixels Red
//   SetData(1, nullptr, blue4,   0,  4)  → mip 1: all  4 pixels Blue
//   SetData(2, nullptr, green1,  0,  1)  → mip 2: the  1 pixel  Green
//   GetData(0, nullptr, rb0,     0, 16)  → verify 16 Red
//   GetData(1, nullptr, rb1,     0,  4)  → verify  4 Blue
//   GetData(2, nullptr, rb2,     0,  1)  → verify  1 Green
//
// Colour values must not bleed across levels.
// No framebuffer readback — pure CPU shadow buffer (cpuPixels_ / extraMipLevels_).
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kRed  (255,   0,   0, 255);
static const Color kBlue (  0,   0, 255, 255);
static const Color kGreen(  0, 255,   0, 255);
static const Color kBlack(  0,   0,   0, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

class MipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;

    void check(bool ok, const char* label, Color got, Color want)
    {
        std::printf("[%s] %s: want=(%d,%d,%d) got=(%d,%d,%d)\n",
            ok ? "PASS" : "FAIL", label,
            want.getRProperty(), want.getGProperty(), want.getBProperty(),
            got.getRProperty(),  got.getGProperty(),  got.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        // 4×4 texture with full mip chain: levels 0 (4×4), 1 (2×2), 2 (1×1)
        Texture2D tex(dev, 4, 4, /*mipMap=*/true, SurfaceFormat::Color);

        // ── write distinct colours to each level ─────────────────────────────
        std::vector<Color> red16 (16, kRed);
        std::vector<Color> blue4 ( 4, kBlue);
        std::vector<Color> green1( 1, kGreen);

        tex.SetData(0, nullptr, red16.data(),  0, 16);
        tex.SetData(1, nullptr, blue4.data(),  0,  4);
        tex.SetData(2, nullptr, green1.data(), 0,  1);

        // ── mip 0: read back 16 pixels — all must be Red ─────────────────────
        std::puts("--- mip 0 (4×4): expect all Red ---");
        {
            std::vector<Color> rb(16, kBlack);
            tex.GetData(0, nullptr, rb.data(), 0, 16);
            for (int i = 0; i < 16; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "mip0 pixel(%d,%d)", i % 4, i / 4);
                check(colourEq(rb[i], kRed), lbl, rb[i], kRed);
            }
        }

        // ── mip 1: read back 4 pixels — all must be Blue ─────────────────────
        std::puts("--- mip 1 (2×2): expect all Blue ---");
        {
            std::vector<Color> rb(4, kBlack);
            tex.GetData(1, nullptr, rb.data(), 0, 4);
            for (int i = 0; i < 4; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "mip1 pixel(%d,%d)", i % 2, i / 2);
                check(colourEq(rb[i], kBlue), lbl, rb[i], kBlue);
            }
        }

        // ── mip 2: read back 1 pixel — must be Green ─────────────────────────
        std::puts("--- mip 2 (1×1): expect Green ---");
        {
            std::vector<Color> rb(1, kBlack);
            tex.GetData(2, nullptr, rb.data(), 0, 1);
            check(colourEq(rb[0], kGreen), "mip2 pixel(0,0)", rb[0], kGreen);
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    MipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    MipTest game;
    game.Run();
    return game.getResult();
}
