// SPDX-License-Identifier: MS-PL
// WEBGPU-113: verify WebGPUTextureCubeBackend::GetData() -- real per-face CPU readback, closing
// this class's former no-op ITextureCubeBackend::GetData() default (WEBGPU-56/74 only added
// SetData(), sufficient for EnvironmentMapEffect.EnvironmentMap but not for reading a TextureCube
// back). Uses the real Microsoft::Xna::Framework::Graphics::TextureCube API directly --
// TextureCube::GetData() (unlike Texture2D::GetData()) has no CPU-side pixel shadow of its own at
// all and always calls straight through to ITextureCubeBackend::GetData(), so this is a genuine
// end-to-end XNA-layer test, not a backend-only one.
//
// Check A -- each of the 6 faces uploaded a distinct solid colour via SetData(); GetData() on
//   each face reads back exactly that face's own colour -- proves per-face addressing (origin.z
//   = face) is correct and faces do not collide/alias each other.
// Check B -- a sub-rectangle read (not the whole face) at a non-origin offset matches exactly the
//   source pixels at that offset.
// Check C -- mip level round trip: level 1 of PositiveX uploaded with distinct content from level
//   0 of the same face; GetData(level=1) reads back level 1's own content -- proves the mip level
//   parameter is honoured, and that a level>0 upload/readback (beyond the pre-allocated-but-never-
//   read/written mip levels WEBGPU-56/74 left completely untested) actually works.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kFaceSize = 4;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    const CubeMapFace kFaces[6] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    const char* kFaceNames[6] = { "PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ" };

    // Distinct, easily-told-apart solid colours for each of the 6 faces.
    const Color kFaceColors[6] = {
        Color(255, 0, 0, 255),     // +X red
        Color(0, 255, 0, 255),     // -X green
        Color(0, 0, 255, 255),     // +Y blue
        Color(255, 255, 0, 255),   // -Y yellow
        Color(255, 0, 255, 255),   // +Z magenta
        Color(0, 255, 255, 255),   // -Z cyan
    };
}

class WebGpuTextureCubeGetDataTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();

        // Checks A/B share one cube map (mipMap=true -> 4->2->1, 3 levels).
        TextureCube cube(dev, kFaceSize, true, SurfaceFormat::Color);

        std::vector<Color> faceData(static_cast<std::size_t>(kFaceSize) * kFaceSize, Color(0, 0, 0, 0));
        for (int f = 0; f < 6; ++f)
        {
            std::fill(faceData.begin(), faceData.end(), kFaceColors[f]);
            cube.SetData(kFaces[f], faceData.data(), static_cast<int>(faceData.size()));
        }

        // Check A: each face reads back its own colour, none other.
        for (int f = 0; f < 6; ++f)
        {
            std::vector<Color> readback(static_cast<std::size_t>(kFaceSize) * kFaceSize, Color(0, 0, 0, 0));
            cube.GetData(kFaces[f], readback.data(), static_cast<int>(readback.size()));

            bool allMatch = true;
            for (const Color& c : readback)
                if (!(c == kFaceColors[f])) allMatch = false;

            check(allMatch, (std::string("Check A: face ") + kFaceNames[f] +
                             " GetData() reads back its own uploaded colour, not another face's").c_str());
        }

        // Check B: sub-rectangle at a non-origin offset on the PositiveX face -- upload a
        // per-pixel-distinct pattern (not a flat colour) first so a shifted/garbled read would be
        // caught, then read back a 2x2 rectangle at (1,1).
        {
            std::vector<Color> pattern(static_cast<std::size_t>(kFaceSize) * kFaceSize, Color(0, 0, 0, 0));
            for (int y = 0; y < kFaceSize; ++y)
                for (int x = 0; x < kFaceSize; ++x)
                    pattern[y * kFaceSize + x] = Color(x * 60, y * 60, 128, 255);
            cube.SetData(CubeMapFace::PositiveX, 0, nullptr, pattern.data(), 0, static_cast<int>(pattern.size()));

            const Rectangle rect(1, 1, 2, 2);
            std::vector<Color> readback(4, Color(0, 0, 0, 0));
            cube.GetData(CubeMapFace::PositiveX, 0, &rect, readback.data(), 0, 4);

            bool match = true;
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 2; ++col)
                    if (!(readback[row * 2 + col] == pattern[(1 + row) * kFaceSize + (1 + col)]))
                        match = false;

            check(match, "Check B: 2x2 sub-rectangle at offset (1,1) on PositiveX matches exactly "
                        "those source pixels, not the whole face / a shifted region");
        }

        // Check C: mip level round trip -- level 1 (2x2) uploaded with content distinct from
        // level 0 (4x4)'s own PositiveX pattern above.
        {
            std::vector<Color> level1(4, Color(10, 200, 30, 255));
            cube.SetData(CubeMapFace::PositiveX, 1, nullptr, level1.data(), 0, 4);

            std::vector<Color> readback(4, Color(0, 0, 0, 0));
            cube.GetData(CubeMapFace::PositiveX, 1, nullptr, readback.data(), 0, 4);

            bool match = true;
            for (int i = 0; i < 4; ++i)
                if (!(readback[i] == level1[i])) match = false;

            check(match, "Check C: GetData(level=1) on PositiveX reads back level 1's own "
                        "uploaded content, not level 0's -- proves the mip level parameter is honoured");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuTextureCubeGetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuTextureCubeGetDataTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
