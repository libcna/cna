// SPDX-License-Identifier: MS-PL
// Task 274: Texture3D partial box GetData — genuine x/y/z sub-region readback.
//
// Task 273's test verifies SetData box placement using a binary Red/Blue split, which would not
// catch a GetData-side bug that reads the right shape from the wrong offset (e.g. an axis swap
// that still happens to preserve a symmetric colour split). This test instead fills the whole
// volume with a per-voxel value that is unique to its (x,y,z) coordinate — R = 20+x*40,
// G = 20+y*60, B = 20+z*40 — so any GetData box that reads the wrong voxels is detectable, not
// just boxes that cross a colour boundary.
//
// Sub-test A — asymmetric off-origin box read:
//   4×3×5 volume filled with the per-voxel formula above. GetData reads a 2×2×3 box at
//   left=1,top=1,front=1 with elementCount exactly matching the box volume (12), and every
//   returned element is checked against the source formula at its (x,y,z).
//
// Sub-test B — GetData with non-zero startIndex:
//   A 2×1×2 box (elementCount=4) is read into the middle of a 6-element output array pre-filled
//   with a sentinel colour; verifies out[0] and out[5] stay untouched (sentinel) and out[1..4]
//   match the box's 4 voxels in x-fastest/y/z-slowest order.
//
// Sub-test C — box touching the far corner (right==width, bottom==height, back==depth):
//   A 2×2×2 box from (2,1,3) to (4,3,5) in the same 4×3×5 volume, verifying the exclusive
//   right/bottom/back bounds are honoured at the far edge on the read path.
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

static const Color kSentinel(1, 2, 3, 255);

static bool colourEq(Color a, Color b)
{
    return a.getRProperty() == b.getRProperty()
        && a.getGProperty() == b.getGProperty()
        && a.getBProperty() == b.getBProperty();
}

// Per-voxel value unique to (x,y,z), distinguishable across every axis independently.
static Color voxelColour(int x, int y, int z)
{
    return Color(20 + x * 40, 20 + y * 60, 20 + z * 40, 255);
}

class Texture3DPartialBoxReadbackTest : public Game
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

        static constexpr int kW = 4, kH = 3, kD = 5;
        Texture3D tex(dev, kW, kH, kD, /*mipMap=*/false, SurfaceFormat::Color);

        // Fill the whole volume with the per-voxel formula (x fastest, y next, z slowest).
        std::vector<Color> full(kW * kH * kD, Color(0, 0, 0, 0));
        for (int z = 0; z < kD; ++z)
        for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            full[x + y * kW + z * kW * kH] = voxelColour(x, y, z);
        tex.SetData(0, 0, 0, kW, kH, 0, kD, full.data(), 0, kW * kH * kD);

        // ════════════════════════════════════════════════════════════════════
        // Sub-test A: asymmetric off-origin box read
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 274A: asymmetric off-origin box read ---");
            const int left = 1, top = 1, front = 1;
            const int bw = 2, bh = 2, bd = 3; // right=3, bottom=3, back=4

            std::vector<Color> box(bw * bh * bd, Color(0, 0, 0, 0));
            tex.GetData(0, left, top, left + bw, top + bh, front, front + bd,
                        box.data(), 0, bw * bh * bd);

            for (int bz = 0; bz < bd; ++bz)
            for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx)
            {
                const int i = bx + by * bw + bz * bw * bh;
                const Color want = voxelColour(left + bx, top + by, front + bz);
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "T274A box(%d,%d,%d)", bx, by, bz);
                check(colourEq(box[i], want), lbl, box[i], want);
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Sub-test B: GetData with non-zero startIndex
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 274B: GetData startIndex ---");
            const int left = 0, top = 0, front = 0;
            const int bw = 2, bh = 1, bd = 2; // elementCount = 4

            std::vector<Color> out(6, kSentinel);
            tex.GetData(0, left, top, left + bw, top + bh, front, front + bd,
                        out.data(), 1, bw * bh * bd);

            check(colourEq(out[0], kSentinel), "T274B out[0] (sentinel)", out[0], kSentinel);
            for (int bz = 0; bz < bd; ++bz)
            for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx)
            {
                const int i = 1 + bx + by * bw + bz * bw * bh;
                const Color want = voxelColour(left + bx, top + by, front + bz);
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "T274B out[%d]", i);
                check(colourEq(out[i], want), lbl, out[i], want);
            }
            check(colourEq(out[5], kSentinel), "T274B out[5] (sentinel)", out[5], kSentinel);
        }

        // ════════════════════════════════════════════════════════════════════
        // Sub-test C: box touching the far corner (right==width, bottom==height, back==depth)
        // ════════════════════════════════════════════════════════════════════
        {
            std::puts("--- Task 274C: far-corner box read ---");
            const int left = 2, top = 1, front = 3;   // right=4=W, bottom=3=H, back=5=D
            const int bw = 2, bh = 2, bd = 2;

            std::vector<Color> box(bw * bh * bd, Color(0, 0, 0, 0));
            tex.GetData(0, left, top, kW, kH, front, kD, box.data(), 0, bw * bh * bd);

            for (int bz = 0; bz < bd; ++bz)
            for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx)
            {
                const int i = bx + by * bw + bz * bw * bh;
                const Color want = voxelColour(left + bx, top + by, front + bz);
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "T274C box(%d,%d,%d)", bx, by, bz);
                check(colourEq(box[i], want), lbl, box[i], want);
            }
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    Texture3DPartialBoxReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    Texture3DPartialBoxReadbackTest game;
    game.Run();
    return game.getResult();
}
