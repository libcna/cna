// SPDX-License-Identifier: MS-PL
// Task 862/864: Texture3D mip-level SetData/GetData round-trip.
//
// A 4x4x4 Texture3D with mipMap=true has 3 mip levels (Texture3D.cpp's CalculateMipLevels(w,h)
// only considers width/height, matching FNA's Texture3D constructor exactly, but each level's
// GPU storage still halves in all 3 dimensions, standard volume-mip behavior): 4x4x4 (mip 0),
// 2x2x2 (mip 1), 1x1x1 (mip 2) — mirrors Task 171's Texture2D mip test and Task 276's
// TextureCube mip test (easygl_texturecube_mip_test.cpp), applied to the 3rd texture type.
//
// Test plan:
//   SetData(0, full box, <red>,    0, 64) -> mip 0: 64 texels of Red
//   SetData(1, full box, <white>,  0,  8) -> mip 1: 8 texels of White (shared marker)
//   SetData(2, full box, <orange>, 0,  1) -> mip 2: 1 texel of Orange (shared marker)
//
// All writes happen first; only then is every level read back and verified.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kRed   (255,   0,   0, 255);
static const Color kWhite (255, 255, 255, 255);
static const Color kOrange(255, 128,   0, 255);
static const Color kGray  (128, 128, 128, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

class Texture3DMipTest : public Game
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

        // 4x4x4, full mip chain: levels 0 (4x4x4), 1 (2x2x2), 2 (1x1x1).
        Texture3D vol(dev, 4, 4, 4, /*mipMap=*/true, SurfaceFormat::Color);

        std::vector<Color> red64  (64, kRed);
        std::vector<Color> white8 ( 8, kWhite);
        std::vector<Color> orange1( 1, kOrange);

        // -- write every mip level --
        vol.SetData(0, 0, 0, 4, 4, 0, 4, red64.data(),   0, 64);
        vol.SetData(1, 0, 0, 2, 2, 0, 2, white8.data(),  0,  8);
        vol.SetData(2, 0, 0, 1, 1, 0, 1, orange1.data(), 0,  1);

        // -- read back every mip level and verify --
        {
            std::vector<Color> rb(64, kGray);
            vol.GetData(0, 0, 0, 4, 4, 0, 4, rb.data(), 0, 64);
            for (int i = 0; i < 64; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "mip0 texel %d", i);
                check(colourEq(rb[i], kRed), lbl, rb[i], kRed);
            }
        }
        {
            std::vector<Color> rb(8, kGray);
            vol.GetData(1, 0, 0, 2, 2, 0, 2, rb.data(), 0, 8);
            for (int i = 0; i < 8; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "mip1 texel %d", i);
                check(colourEq(rb[i], kWhite), lbl, rb[i], kWhite);
            }
        }
        {
            std::vector<Color> rb(1, kGray);
            vol.GetData(2, 0, 0, 1, 1, 0, 1, rb.data(), 0, 1);
            check(colourEq(rb[0], kOrange), "mip2 texel 0", rb[0], kOrange);
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    Texture3DMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    Texture3DMipTest game;
    game.Run();
    return game.getResult();
}
