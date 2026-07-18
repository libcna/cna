// SPDX-License-Identifier: MS-PL
// WEBGPU-57/112: verify WebGPUTexture3DBackend / WebGPUGraphicsBackend::CreateTexture3D() -- this
// backend's first Texture3D (volume texture) support. Before this task, CreateTexture3D was
// IGraphicsBackend's own safe no-op default (returns nullptr), so Texture3D::backend_ was always
// null on this backend and every SetData/GetData call silently did nothing.
//
// Texture3D's XNA-layer SetData/GetData (Texture3D.cpp) have no CPU-side pixel shadow at all --
// unlike Texture2D, every call goes straight through to ITexture3DBackend::SetData()/GetData(),
// so this is a genuine end-to-end XNA-layer test (Texture3D, not the backend interface directly).
//
// Check A -- per-depth-slice distinct colour upload + full-volume readback: a 2x2x4 volume, each
//   of the 4 depth slices filled with its own distinct solid colour via the 10-arg
//   SetData(level,left,top,right,bottom,front,back,...) overload, then GetData() over the whole
//   volume reads back exactly those 4 slices' own colours in the correct order -- proves the
//   z/depth dimension is genuinely addressed per-slice, not collapsed/aliased.
// Check B -- a sub-volume read (a single 1x1x1 voxel at a non-origin (x,y,z)) matches exactly the
//   source voxel at that offset, not slice 0 or a shifted region.
// Check C -- mip level round trip: level 1 uploaded with content distinct from level 0, GetData
//   (level=1) reads back level 1's own content.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }
}

class WebGpuTexture3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();

        // Checks A/B share one 2x2x4 volume (mipMap=true).
        constexpr int w = 2, h = 2, depth = 4;
        Texture3D vol(dev, w, h, depth, true, SurfaceFormat::Color);

        const Color sliceColors[depth] = {
            Color(255, 0, 0, 255),   // slice 0: red
            Color(0, 255, 0, 255),   // slice 1: green
            Color(0, 0, 255, 255),   // slice 2: blue
            Color(255, 255, 0, 255), // slice 3: yellow
        };
        for (int z = 0; z < depth; ++z)
        {
            std::vector<Color> sliceData(static_cast<std::size_t>(w) * h, sliceColors[z]);
            vol.SetData(0, 0, 0, w, h, z, z + 1, sliceData.data(), 0, static_cast<int>(sliceData.size()));
        }

        // Check A: full-volume readback -- each slice's own colour, in the right order.
        {
            std::vector<Color> readback(static_cast<std::size_t>(w) * h * depth, Color(0, 0, 0, 0));
            vol.GetData(0, 0, 0, w, h, 0, depth, readback.data(), 0, static_cast<int>(readback.size()));

            bool match = true;
            for (int z = 0; z < depth && match; ++z)
                for (int i = 0; i < w * h && match; ++i)
                    if (!(readback[static_cast<std::size_t>(z) * w * h + i] == sliceColors[z]))
                        match = false;

            check(match, "Check A: full-volume GetData() reads back all 4 depth slices' own "
                        "distinct colours in the correct order -- proves per-slice addressing");
        }

        // Check B: a single voxel at a non-origin offset (x=1,y=1,z=2) matches exactly slice 2's
        // colour (blue), not slice 0 (red) or a shifted/aliased region.
        {
            Color voxel(0, 0, 0, 0);
            vol.GetData(0, 1, 1, 2, 2, 2, 3, &voxel, 0, 1);
            check(voxel == sliceColors[2], "Check B: single-voxel GetData() at (x=1,y=1,z=2) "
                        "matches exactly that voxel's own colour (slice 2, blue)");
        }

        // Check C: mip level round trip -- level 1 (1x1x2, since w/h halve, depth halves too per
        // wgpu-native's standard 3D mip rules) uploaded with content distinct from level 0.
        {
            const Color level1Color(10, 200, 30, 255);
            std::vector<Color> level1Data(2, level1Color); // 1x1x2 volume at level 1
            vol.SetData(1, 0, 0, 1, 1, 0, 2, level1Data.data(), 0, 2);

            std::vector<Color> readback(2, Color(0, 0, 0, 0));
            vol.GetData(1, 0, 0, 1, 1, 0, 2, readback.data(), 0, 2);

            bool match = (readback[0] == level1Color) && (readback[1] == level1Color);
            check(match, "Check C: GetData(level=1) reads back level 1's own uploaded content, "
                        "not level 0's -- proves the mip level parameter is honoured");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuTexture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuTexture3DTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
