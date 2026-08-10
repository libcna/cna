// SPDX-License-Identifier: MS-PL
// Task 173: Texture3D z-slice SetData / GetData round-trip.
//
// A 2×2×4 Texture3D (mipMap=false, SurfaceFormat::Color) is created.
// Each z-slice is written with a distinct colour:
//
//   z=0 → Red
//   z=1 → Green
//   z=2 → Blue
//   z=3 → Yellow (255,255,0)
//
// After writing all 4 slices, each slice is read back individually via the
// 9-arg GetData overload and every pixel is compared against the expected colour.
// Colours must not bleed across slices.
//
// SetData uses the 9-arg overload: SetData(level, left, top, right, bottom, front, back, ...)
// GetData uses the 9-arg overload: GetData(level, left, top, right, bottom, front, back, ...)
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kRed   (255,   0,   0, 255);
static const Color kGreen (  0, 255,   0, 255);
static const Color kBlue  (  0,   0, 255, 255);
static const Color kYellow(255, 255,   0, 255);
static const Color kBlack (  0,   0,   0, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

struct SliceEntry { int z; const char* name; Color colour; };

static const std::array<SliceEntry, 4> kSlices = {{
    { 0, "z=0", kRed    },
    { 1, "z=1", kGreen  },
    { 2, "z=2", kBlue   },
    { 3, "z=3", kYellow },
}};

class Texture3DSlicesTest : public Game
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

        static constexpr int kW = 2, kH = 2, kD = 4;   // 2×2×4
        Texture3D tex(dev, kW, kH, kD, /*mipMap=*/false, SurfaceFormat::Color);

        // ── write each z-slice a distinct colour ─────────────────────────────
        for (const auto& se : kSlices)
        {
            // SetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)
            // front=z, back=z+1 → single slice
            std::vector<Color> pixels(kW * kH, se.colour);
            tex.SetData(0, 0, 0, kW, kH, se.z, se.z + 1,
                        pixels.data(), 0, kW * kH);
        }

        // ── read back each slice and verify every pixel ───────────────────────
        for (const auto& se : kSlices)
        {
            std::printf("--- slice %s ---\n", se.name);
            // GetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)
            std::vector<Color> rb(kW * kH, kBlack);
            tex.GetData(0, 0, 0, kW, kH, se.z, se.z + 1,
                        rb.data(), 0, kW * kH);

            for (int i = 0; i < kW * kH; ++i)
            {
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "%s pixel(%d,%d)",
                    se.name, i % kW, i / kW);
                check(colourEq(rb[i], se.colour), lbl, rb[i], se.colour);
            }
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    Texture3DSlicesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    Texture3DSlicesTest game;
    game.Run();
    return game.getResult();
}
