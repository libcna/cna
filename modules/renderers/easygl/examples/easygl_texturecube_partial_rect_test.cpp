// SPDX-License-Identifier: MS-PL
// Task 275: TextureCube face upload/readback — partial rect and startIndex, all six faces.
//
// Task 172's existing test (easygl_texturecube_faces_test.cpp) already gives pixel-exact
// round-trip coverage for the simple whole-face SetData(face,data,elementCount)/
// GetData(face,data,elementCount) overloads across all 6 faces. What it does not cover — and what
// TextureCubeTests.cpp's argument-guard tests don't either — is genuine *value* verification of
// the rect-based 6-arg overload (SetData/GetData(face,level,rect,...)) added/fixed by the Task 272
// audit, or the startIndex overload with real data. This test closes that gap.
//
// Sub-test A — partial rect round-trip, all six faces:
//   4×4 TextureCube. Each face is filled with its own distinct background colour (same palette as
//   Task 172), then a 2×2 White rect is written at an off-centre, asymmetric position (rect(1,0,2,2))
//   via the 6-arg SetData overload. A full-face GetData afterwards verifies: the 4 rect pixels are
//   White, all 12 others are that face's own background colour, and — since every face is checked
//   only after ALL faces have been written — no face's rect write bled into another face.
//
// Sub-test B — SetData with non-zero startIndex (rect-based, PositiveX face):
//   Mirrors Task 170A's Texture2D pattern: a 6-element source array with Green padding around a
//   2-element Blue payload; SetData(face,0,&rect(1,0,2,1),src6,startIndex=2,elementCount=2) must
//   only consume the 2 Blue elements.
//
// Sub-test C — GetData with non-zero startIndex (rect-based, PositiveX face):
//   Mirrors Task 170B's Texture2D pattern: GetData into the middle of a sentinel-padded array,
//   verifying the padding stays untouched.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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
static const Color kBlack  (  0,   0,   0, 255);

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

class TextureCubePartialRectTest : public Game
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

        static constexpr int kSize = 4; // 4x4 per face → 16 pixels per face

        // ════════════════════════════════════════════════════════════════════
        // Sub-test A: partial rect round-trip, all six faces
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 275A: partial rect, all six faces ---");
            TextureCube cube(dev, kSize, /*mipMap=*/false, SurfaceFormat::Color);

            // Fill every face's background first.
            for (const auto& fe : kFaces)
            {
                std::vector<Color> bg(kSize * kSize, fe.colour);
                cube.SetData(fe.face, bg.data(), kSize * kSize);
            }

            // Write an off-centre 2x2 White rect into every face.
            const Rectangle subRect(1, 0, 2, 2);
            const Color white4[4] = { kWhite, kWhite, kWhite, kWhite };
            for (const auto& fe : kFaces)
                cube.SetData(fe.face, 0, &subRect, white4, 0, 4);

            // Verify every face: only the rect pixels are White, everything else is that
            // face's own background — and no cross-face bleed occurred.
            const int whiteIdx[4] = { 1, 2, 5, 6 }; // (1,0) (2,0) (1,1) (2,1) in a 4-wide face
            auto isWhite = [&](int i) {
                for (int w : whiteIdx) if (w == i) return true;
                return false;
            };
            for (const auto& fe : kFaces)
            {
                std::vector<Color> rb(kSize * kSize, kBlack);
                cube.GetData(fe.face, rb.data(), kSize * kSize);
                for (int i = 0; i < kSize * kSize; ++i)
                {
                    char lbl[96];
                    std::snprintf(lbl, sizeof(lbl), "T275A %s pixel(%d,%d)",
                        fe.name, i % kSize, i / kSize);
                    const Color want = isWhite(i) ? kWhite : fe.colour;
                    check(colourEq(rb[i], want), lbl, rb[i], want);
                }
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Sub-test B: SetData with non-zero startIndex (rect-based, PositiveX)
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 275B: SetData startIndex (rect-based) ---");
            TextureCube cube(dev, 4, /*mipMap=*/false, SurfaceFormat::Color);

            std::vector<Color> bg(16, kRed);
            cube.SetData(CubeMapFace::PositiveX, bg.data(), 16);

            // 6-element array: [Green, Green, Blue, Blue, Green, Green]
            // startIndex=2, elementCount=2 → data[2]=Blue, data[3]=Blue into rect(1,0,2,1)
            const Color src6[6] = { kGreen, kGreen, kBlue, kBlue, kGreen, kGreen };
            const Rectangle rect2x1(1, 0, 2, 1);
            cube.SetData(CubeMapFace::PositiveX, 0, &rect2x1, src6, 2, 2);

            std::vector<Color> rb(16, kBlack);
            cube.GetData(CubeMapFace::PositiveX, rb.data(), 16);

            for (int i = 0; i < 16; ++i)
            {
                const bool inRect = (i == 1 || i == 2); // (1,0) and (2,0)
                const Color want = inRect ? kBlue : kRed;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T275B pixel(%d,%d)", i % 4, i / 4);
                check(colourEq(rb[i], want), lbl, rb[i], want);
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Sub-test C: GetData with non-zero startIndex (rect-based, PositiveX)
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 275C: GetData startIndex (rect-based) ---");
            TextureCube cube(dev, 4, /*mipMap=*/false, SurfaceFormat::Color);

            std::vector<Color> bg(16, kRed);
            cube.SetData(CubeMapFace::PositiveX, bg.data(), 16);
            const Color src6[6] = { kGreen, kGreen, kBlue, kBlue, kGreen, kGreen };
            const Rectangle rect2x1(1, 0, 2, 1);
            cube.SetData(CubeMapFace::PositiveX, 0, &rect2x1, src6, 2, 2);

            // GetData with startIndex=1: writes into out[1], out[2]; out[0]/out[3] untouched.
            std::vector<Color> out(4, kGreen); // sentinel
            cube.GetData(CubeMapFace::PositiveX, 0, &rect2x1, out.data(), 1, 2);

            const Color wantOut[4] = { kGreen, kBlue, kBlue, kGreen };
            for (int i = 0; i < 4; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T275C out[%d]", i);
                check(colourEq(out[i], wantOut[i]), lbl, out[i], wantOut[i]);
            }
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    TextureCubePartialRectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    TextureCubePartialRectTest game;
    game.Run();
    return game.getResult();
}
