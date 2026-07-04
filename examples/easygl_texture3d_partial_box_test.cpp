// SPDX-License-Identifier: MS-PL
// Task 273: Texture3D partial box SetData — genuine x/y/z sub-region upload.
//
// Task 173's existing test (easygl_texture3d_slices_test.cpp) only exercises boxes that span
// the full width/height and vary only in z (front/back) — it would not catch a bug where x or y
// offsets/extents are swapped or ignored. This test instead uses boxes with distinct width,
// height, and depth, offset away from the origin on every axis, so a transposed or dropped
// coordinate produces a detectable mismatch.
//
// Sub-test A — asymmetric off-origin box:
//   4×5×3 volume (deliberately distinct W/H/D), filled entirely Red.
//   A 2×3×2 Blue box is written at left=1,top=2,front=1 (right=3,bottom=5,back=3).
//   Full-volume GetData verifies every one of the 60 voxels: Blue iff x∈{1,2} ∧ y∈{2,3,4} ∧
//   z∈{1,2}; Red everywhere else.
//
// Sub-test B — single-voxel box:
//   3×3×3 volume, filled entirely Red. A 1×1×1 Green box is written at (2,1,0).
//   Full-volume GetData verifies exactly voxel (2,1,0) is Green; all 26 others are Red.
//
// Sub-test C — box touching the far corner (right==width, bottom==height, back==depth):
//   4×4×4 volume, filled entirely Red. A box from (2,2,2) to (4,4,4) is written Yellow.
//   Verifies the exclusive right/bottom/back bounds are handled correctly at the far edge.
//
// No framebuffer/screen readback needed for SetData verification beyond Texture3D::GetData
// itself (which round-trips through a temporary FBO internally — see EasyGLTexture3DBackend).
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
static const Color kBlue  (  0,   0, 255, 255);
static const Color kGreen (  0, 255,   0, 255);
static const Color kYellow(255, 255,   0, 255);
static const Color kBlack (  0,   0,   0, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

class Texture3DPartialBoxTest : public Game
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
        // Sub-test A: asymmetric off-origin box (distinct W/H/D, offset on every axis)
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 273A: asymmetric off-origin box ---");
            static constexpr int kW = 4, kH = 5, kD = 3;
            Texture3D tex(dev, kW, kH, kD, /*mipMap=*/false, SurfaceFormat::Color);

            std::vector<Color> allRed(kW * kH * kD, kRed);
            tex.SetData(0, 0, 0, kW, kH, 0, kD, allRed.data(), 0, kW * kH * kD);

            // Box: left=1,right=3 (w=2)  top=2,bottom=5 (h=3)  front=1,back=3 (d=2)
            const int bw = 2, bh = 3, bd = 2;
            std::vector<Color> blueBox(bw * bh * bd, kBlue);
            tex.SetData(0, 1, 2, 3, 5, 1, 3, blueBox.data(), 0, bw * bh * bd);

            std::vector<Color> rb(kW * kH * kD, kBlack);
            tex.GetData(0, 0, 0, kW, kH, 0, kD, rb.data(), 0, kW * kH * kD);

            for (int z = 0; z < kD; ++z)
            for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
            {
                const int i = x + y * kW + z * kW * kH;
                const bool inBox = (x >= 1 && x < 3) && (y >= 2 && y < 5) && (z >= 1 && z < 3);
                const Color want = inBox ? kBlue : kRed;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T273A voxel(%d,%d,%d)", x, y, z);
                check(colourEq(rb[i], want), lbl, rb[i], want);
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Sub-test B: single-voxel box
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 273B: single-voxel box ---");
            static constexpr int kN = 3;
            Texture3D tex(dev, kN, kN, kN, /*mipMap=*/false, SurfaceFormat::Color);

            std::vector<Color> allRed(kN * kN * kN, kRed);
            tex.SetData(0, 0, 0, kN, kN, 0, kN, allRed.data(), 0, kN * kN * kN);

            // Single voxel at (x=2,y=1,z=0)
            const Color green1[1] = { kGreen };
            tex.SetData(0, 2, 1, 3, 2, 0, 1, green1, 0, 1);

            std::vector<Color> rb(kN * kN * kN, kBlack);
            tex.GetData(0, 0, 0, kN, kN, 0, kN, rb.data(), 0, kN * kN * kN);

            for (int z = 0; z < kN; ++z)
            for (int y = 0; y < kN; ++y)
            for (int x = 0; x < kN; ++x)
            {
                const int i = x + y * kN + z * kN * kN;
                const bool isTarget = (x == 2 && y == 1 && z == 0);
                const Color want = isTarget ? kGreen : kRed;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T273B voxel(%d,%d,%d)", x, y, z);
                check(colourEq(rb[i], want), lbl, rb[i], want);
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Sub-test C: box touching the far corner (right==width, bottom==height, back==depth)
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 273C: far-corner box ---");
            static constexpr int kN = 4;
            Texture3D tex(dev, kN, kN, kN, /*mipMap=*/false, SurfaceFormat::Color);

            std::vector<Color> allRed(kN * kN * kN, kRed);
            tex.SetData(0, 0, 0, kN, kN, 0, kN, allRed.data(), 0, kN * kN * kN);

            // Box from (2,2,2) to (4,4,4) — touches the far corner exactly.
            const int bw = 2, bh = 2, bd = 2;
            std::vector<Color> yellowBox(bw * bh * bd, kYellow);
            tex.SetData(0, 2, 2, 4, 4, 2, 4, yellowBox.data(), 0, bw * bh * bd);

            std::vector<Color> rb(kN * kN * kN, kBlack);
            tex.GetData(0, 0, 0, kN, kN, 0, kN, rb.data(), 0, kN * kN * kN);

            for (int z = 0; z < kN; ++z)
            for (int y = 0; y < kN; ++y)
            for (int x = 0; x < kN; ++x)
            {
                const int i = x + y * kN + z * kN * kN;
                const bool inBox = (x >= 2 && x < 4) && (y >= 2 && y < 4) && (z >= 2 && z < 4);
                const Color want = inBox ? kYellow : kRed;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "T273C voxel(%d,%d,%d)", x, y, z);
                check(colourEq(rb[i], want), lbl, rb[i], want);
            }
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    Texture3DPartialBoxTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    Texture3DPartialBoxTest game;
    game.Run();
    return game.getResult();
}
