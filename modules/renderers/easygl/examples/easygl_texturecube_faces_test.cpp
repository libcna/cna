// SPDX-License-Identifier: MS-PL
// Task 172: TextureCube per-face SetData / GetData round-trip.
//
// A 2×2 TextureCube (mipMap=false) is created and each of the 6 faces is
// filled with a distinct colour:
//
//   PositiveX → Red
//   NegativeX → Green
//   PositiveY → Blue
//   NegativeY → Yellow  (255,255,0)
//   PositiveZ → Cyan    (0,255,255)
//   NegativeZ → Magenta (255,0,255)
//
// After writing all 6 faces, each face is read back and every pixel is
// compared against the expected colour.  Colours must not bleed across faces.
//
// GetData is exercised via the 2-arg overload (face, data[], elementCount) so
// the GL FBO readback path is tested end-to-end.
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

class CubeFacesTest : public Game
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

        static constexpr int kSize = 2;   // 2×2 per face → 4 pixels per face
        TextureCube cube(dev, kSize, /*mipMap=*/false, SurfaceFormat::Color);

        // ── write each face a distinct colour ────────────────────────────────
        for (const auto& fe : kFaces)
        {
            std::vector<Color> pixels(kSize * kSize, fe.colour);
            cube.SetData(fe.face, pixels.data(), kSize * kSize);
        }

        // ── read back each face and verify every pixel ────────────────────────
        for (const auto& fe : kFaces)
        {
            std::printf("--- face %s ---\n", fe.name);
            std::vector<Color> rb(kSize * kSize, kBlack);
            cube.GetData(fe.face, rb.data(), kSize * kSize);

            for (int i = 0; i < kSize * kSize; ++i)
            {
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "%s pixel(%d,%d)",
                    fe.name, i % kSize, i / kSize);
                check(colourEq(rb[i], fe.colour), lbl, rb[i], fe.colour);
            }
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    CubeFacesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    CubeFacesTest game;
    game.Run();
    return game.getResult();
}
