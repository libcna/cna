// SPDX-License-Identifier: MS-PL
// Task 169: Texture2D SetData / GetData — partial rectangle round-trip.
//
// Verifies that writing a sub-rectangle of a texture via
//   SetData(level=0, rect, data, startIndex, elementCount)
// and reading back the full texture via
//   GetData(data, 0, pixelCount)
// correctly reflects only the changed region.
//
// Design:
//   4×4 texture, filled with Red via SetData(level=0, nullptr, ...).
//   Then a 2×2 Blue sub-rect at (1,1)–(2,2) is written via SetData(level=0, &rect, ...).
//   GetData reads back the full 16 pixels.
//
//   Expected layout (R=Red, B=Blue):
//     R R R R
//     R B B R
//     R B B R
//     R R R R
//
//   Pixel indices in row-major order:
//     0  1  2  3
//     4  5  6  7
//     8  9  10 11
//     12 13 14 15
//
//   Pixels 5, 6, 9, 10 → Blue.  All others → Red.
//
// This test exercises only the CPU-side cpuPixels_ shadow buffer; no
// framebuffer or screen readback is needed.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kRed (255,   0,   0, 255);
static const Color kBlue(  0,   0, 255, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

class PartialRectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        // ── Create 4×4 texture ────────────────────────────────────────────
        Texture2D tex(dev, 4, 4);

        // ── Fill entire texture with Red ──────────────────────────────────
        std::vector<Color> allRed(16, kRed);
        tex.SetData(0, nullptr, allRed.data(), 0, 16);

        // ── Overwrite 2×2 sub-rect at (1,1) with Blue ────────────────────
        const Rectangle subRect(1, 1, 2, 2);
        Color blue4[4] = { kBlue, kBlue, kBlue, kBlue };
        tex.SetData(0, &subRect, blue4, 0, 4);

        // ── Read back full texture ────────────────────────────────────────
        std::vector<Color> readback(16, Color(0, 0, 0, 0));
        tex.GetData(readback.data(), 0, 16);

        // ── Verify ───────────────────────────────────────────────────────
        // Blue pixels: (1,1)=5, (2,1)=6, (1,2)=9, (2,2)=10
        const int blueIdx[4] = { 5, 6, 9, 10 };
        auto isBlueIdx = [&](int i) {
            for (int b : blueIdx) if (b == i) return true;
            return false;
        };

        for (int i = 0; i < 16; ++i)
        {
            const Color want = isBlueIdx(i) ? kBlue : kRed;
            const bool  ok   = colourEq(readback[i], want);
            const int   row  = i / 4;
            const int   col  = i % 4;
            std::printf("[%s] pixel(%d,%d): want=(%d,%d,%d) got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL",
                col, row,
                want.getRProperty(),     want.getGProperty(),     want.getBProperty(),
                readback[i].getRProperty(), readback[i].getGProperty(), readback[i].getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    PartialRectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    PartialRectTest game;
    game.Run();
    return game.getResult();
}
