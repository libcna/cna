// SPDX-License-Identifier: MS-PL
// Tasks 169-170: Texture2D SetData / GetData — partial rectangle and startIndex tests.
//
// Task 169 — partial rectangle round-trip:
//   4×4 texture, filled with Red.  2×2 Blue sub-rect written at (1,1).
//   GetData reads back all 16 pixels; verifies only the 4 sub-rect pixels are Blue.
//
//   Expected layout (R=Red, B=Blue):
//     R R R R    indices: 0  1  2  3
//     R B B R             4  5  6  7
//     R B B R             8  9  10 11
//     R R R R             12 13 14 15
//   → pixels 5, 6, 9, 10 = Blue; all others = Red.
//
// Task 170 — startIndex and elementCount:
//   Sub-test A — SetData with non-zero startIndex:
//     4×1 texture, filled with Red.
//     data array: [Green, Green, Blue, Blue, Green, Green]  (6 elements)
//     SetData(level=0, rect=(1,0,2,1), data, startIndex=2, elementCount=2)
//     → only data[2]=Blue and data[3]=Blue are written; Green elements skipped.
//     GetData full texture → [Red, Blue, Blue, Red].
//
//   Sub-test B — GetData with non-zero startIndex:
//     Using same texture state (after Sub-test A).
//     GetData(level=0, rect=(1,0,2,1), out, startIndex=1, elementCount=2)
//     → writes out[1]=Blue, out[2]=Blue; out[0] and out[3] are untouched.
//
// No framebuffer or screen readback needed — pure CPU-side cpuPixels_ shadow buffer.
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

class PartialRectTest : public Game
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

        // ════════════════════════════════════════════════════════════════════
        // Task 169: partial rectangle round-trip
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 169: partial rectangle ---");
            Texture2D tex(dev, 4, 4);

            std::vector<Color> allRed(16, kRed);
            tex.SetData(0, nullptr, allRed.data(), 0, 16);

            const Rectangle subRect(1, 1, 2, 2);
            const Color blue4[4] = { kBlue, kBlue, kBlue, kBlue };
            tex.SetData(0, &subRect, blue4, 0, 4);

            std::vector<Color> rb(16, kBlack);
            tex.GetData(rb.data(), 0, 16);

            const int blueIdx[4] = { 5, 6, 9, 10 };
            auto isBlue = [&](int i) {
                for (int b : blueIdx) if (b == i) return true;
                return false;
            };
            for (int i = 0; i < 16; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T169 pixel(%d,%d)", i % 4, i / 4);
                const Color want = isBlue(i) ? kBlue : kRed;
                check(colourEq(rb[i], want), lbl, rb[i], want);
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Task 170A: SetData with non-zero startIndex
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 170A: SetData startIndex ---");
            Texture2D tex(dev, 4, 1);

            // Fill 4×1 with Red
            std::vector<Color> allRed(4, kRed);
            tex.SetData(0, nullptr, allRed.data(), 0, 4);

            // 6-element array: [Green, Green, Blue, Blue, Green, Green]
            // startIndex=2, elementCount=2 → write data[2]=Blue, data[3]=Blue
            // into rect (1,0,2,1) → pixels (1,0) and (2,0)
            const Color src6[6] = { kGreen, kGreen, kBlue, kBlue, kGreen, kGreen };
            const Rectangle rect2x1(1, 0, 2, 1);
            tex.SetData(0, &rect2x1, src6, 2, 2);

            std::vector<Color> rb(4, kBlack);
            tex.GetData(rb.data(), 0, 4);

            const Color want4[4] = { kRed, kBlue, kBlue, kRed };
            for (int i = 0; i < 4; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T170A pixel(%d,0)", i);
                check(colourEq(rb[i], want4[i]), lbl, rb[i], want4[i]);
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Task 170B: GetData with non-zero startIndex
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 170B: GetData startIndex ---");
            // Re-create 4×1 texture in same state: Red, Blue, Blue, Red
            Texture2D tex(dev, 4, 1);
            std::vector<Color> allRed(4, kRed);
            tex.SetData(0, nullptr, allRed.data(), 0, 4);
            const Color src6[6] = { kGreen, kGreen, kBlue, kBlue, kGreen, kGreen };
            const Rectangle rect2x1(1, 0, 2, 1);
            tex.SetData(0, &rect2x1, src6, 2, 2);

            // GetData with startIndex=1: reads 2 pixels from rect (1,0,2,1)
            // → writes into out[1] and out[2]; out[0] and out[3] untouched
            std::vector<Color> out(4, kGreen);   // sentinel: pre-fill with Green
            tex.GetData(0, &rect2x1, out.data(), 1, 2);

            // out[0] = Green (sentinel, untouched)
            // out[1] = Blue  (pixel (1,0))
            // out[2] = Blue  (pixel (2,0))
            // out[3] = Green (sentinel, untouched)
            const Color wantOut[4] = { kGreen, kBlue, kBlue, kGreen };
            for (int i = 0; i < 4; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T170B out[%d]", i);
                check(colourEq(out[i], wantOut[i]), lbl, out[i], wantOut[i]);
            }
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
