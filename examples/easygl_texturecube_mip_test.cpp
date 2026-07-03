// SPDX-License-Identifier: MS-PL
// Task 276: TextureCube mip-level SetData/GetData round-trip, all six faces.
//
// A 4x4 TextureCube with mipMap=true has 3 mip levels per face: 4x4 (mip 0), 2x2 (mip 1),
// 1x1 (mip 2) — mirrors Task 171's Texture2D mip test, but repeated across all six faces so a
// per-face vs. per-level indexing bug in the backend is also caught.
//
// Test plan, for every face:
//   SetData(face, 0, nullptr, <face colour>, 0, 16) → mip 0: 16 pixels of that face's own colour
//   SetData(face, 1, nullptr, white4,        0,  4) → mip 1: 4 pixels White (shared marker)
//   SetData(face, 2, nullptr, orange1,       0,  1) → mip 2: 1 pixel Orange (shared marker)
//
// All writes for all six faces happen first; only then is every face/level read back and
// verified, so a cross-face or cross-level bleed would be caught, not just a same-face,
// same-level placement bug.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kRed    (255,   0,   0, 255);
static const Color kGreen  (  0, 255,   0, 255);
static const Color kBlue   (  0,   0, 255, 255);
static const Color kYellow (255, 255,   0, 255);
static const Color kCyan   (  0, 255, 255, 255);
static const Color kMagenta(255,   0, 255, 255);
static const Color kWhite  (255, 255, 255, 255);
static const Color kOrange (255, 128,   0, 255);
static const Color kGray   (128, 128, 128, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

struct FaceEntry { CubeMapFace face; const char* name; Color colour; };

static const std::array<FaceEntry, 6> kFaces = {{
    { CubeMapFace::PositiveX, "PositiveX", kRed     },
    { CubeMapFace::NegativeX, "NegativeX", kGreen   },
    { CubeMapFace::PositiveY, "PositiveY", kBlue    },
    { CubeMapFace::NegativeY, "NegativeY", kYellow  },
    { CubeMapFace::PositiveZ, "PositiveZ", kCyan    },
    { CubeMapFace::NegativeZ, "NegativeZ", kMagenta },
}};

class TextureCubeMipTest : public Game
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

        // 4x4 per face, full mip chain: levels 0 (4x4), 1 (2x2), 2 (1x1)
        TextureCube cube(dev, 4, /*mipMap=*/true, SurfaceFormat::Color);

        std::vector<Color> white4 (4, kWhite);
        std::vector<Color> orange1(1, kOrange);

        // ── write every face's mip chain ──────────────────────────────────────
        for (const auto& fe : kFaces)
        {
            std::vector<Color> base16(16, fe.colour);
            cube.SetData(fe.face, 0, nullptr, base16.data(),  0, 16);
            cube.SetData(fe.face, 1, nullptr, white4.data(),  0,  4);
            cube.SetData(fe.face, 2, nullptr, orange1.data(), 0,  1);
        }

        // ── read back every face's mip chain and verify ───────────────────────
        for (const auto& fe : kFaces)
        {
            std::printf("--- face %s ---\n", fe.name);

            {
                std::vector<Color> rb(16, kGray);
                cube.GetData(fe.face, 0, nullptr, rb.data(), 0, 16);
                for (int i = 0; i < 16; ++i)
                {
                    char lbl[96];
                    std::snprintf(lbl, sizeof(lbl), "%s mip0 pixel(%d,%d)", fe.name, i % 4, i / 4);
                    check(colourEq(rb[i], fe.colour), lbl, rb[i], fe.colour);
                }
            }
            {
                std::vector<Color> rb(4, kGray);
                cube.GetData(fe.face, 1, nullptr, rb.data(), 0, 4);
                for (int i = 0; i < 4; ++i)
                {
                    char lbl[96];
                    std::snprintf(lbl, sizeof(lbl), "%s mip1 pixel(%d,%d)", fe.name, i % 2, i / 2);
                    check(colourEq(rb[i], kWhite), lbl, rb[i], kWhite);
                }
            }
            {
                std::vector<Color> rb(1, kGray);
                cube.GetData(fe.face, 2, nullptr, rb.data(), 0, 1);
                char lbl[96];
                std::snprintf(lbl, sizeof(lbl), "%s mip2 pixel(0,0)", fe.name);
                check(colourEq(rb[0], kOrange), lbl, rb[0], kOrange);
            }
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    TextureCubeMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    TextureCubeMipTest game;
    game.Run();
    return game.getResult();
}
