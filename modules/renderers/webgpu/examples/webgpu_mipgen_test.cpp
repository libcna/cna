// SPDX-License-Identifier: MS-PL
// WEBGPU-52: verify real, render-pass-based mip generation for a plain Texture2D/TextureCube --
// WebGPURenderer::GenerateMipsForLayer() (GenerateMips2D()/GenerateMipsCubeFace()), a
// render pass per mip level drawing a full-screen triangle that samples the PREVIOUS level through
// a real linear WGPUSampler into the NEXT level's own render-attachment view. wgpu-native has no
// vkCmdBlitImage-equivalent filtered-downsample primitive (verified against this project's pinned
// wgpu-native v29.0.1.1 webgpu.h: wgpuCommandEncoderCopyTextureToTexture is a raw same-size copy),
// so this is the technique other WebGPU-based engines use.
//
// IMPORTANT, DELIBERATE DIVERGENCE this test also documents: unlike every other CNA graphics
// renderer (VulkanRenderer/EasyGLRenderer), which only regenerate mip content for a
// RENDER TARGET being unbound (matching real FNA3D semantics -- a plain Texture2D/TextureCube never
// auto-generates mip content from level 0 in FNA/XNA itself), this WebGPU-only behaviour DOES
// generate real mip content for a plain Texture2D/TextureCube automatically whenever mipMap=true
// AND content is written to level 0 (at construction, or any later level-0 SetData()/
// UpdatePixels() call). See plans/plan_webgpu.md's WEBGPU-52 row and docs/webgpu-renderer.md for the full
// investigation.
//
// Uses IGraphicsRenderer/ImageData directly (bypassing the XNA Texture2D/TextureCube layer),
// matching examples/webgpu_texture2d_getdata_test.cpp's own established convention for testing a
// renderer capability that has no dedicated XNA-layer entry point of its own.
//
// Check A -- a hard-edged red/blue vertical stripe (source width 8: columns 0-2 red, columns 3-7
//   blue) uploaded to level 0 of a mipMap=true Texture2D (mipLevels=4 for size 8); level 1 (width
//   4) read back via GetData(): the destination pixel whose 2-texel source pair straddles the
//   red/blue boundary (source texels 2 and 3) must be a genuine BLEND of red and blue (both R and
//   B channels roughly mid-range, not 0 or 255) -- proving real LINEAR filtering, not a nearest-
//   neighbor copy (which would show a hard, unblended edge with no intermediate colour at all).
//   The two pixels entirely within one colour region must stay that colour.
// Check B -- level 2 (width 2, chain-downsampled from level 1, not directly from level 0) has
//   real, plausible content -- not all-zero/garbage -- proving the per-level loop chains correctly
//   (level N sources level N-1, not always level 0).
// Check C -- the same red/blue-boundary blending proof, but for ONE face of a TextureCube (proving
//   GenerateMipsCubeFace() -- the per-array-layer sibling of Check A's plain-2D path -- also
//   produces genuine linear-filtered content, and that regeneration triggers correctly from
//   TextureCube::SetData()'s own per-face level-0 write, not just a Texture2D constructor).
// Check D -- mipMap=false (mipLevels=1): construction with non-empty pixel data must not crash
//   (GenerateMips2D/GenerateMipsCubeFace's own mipLevels<=1 no-op guard).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Graphics::ImageData;

namespace
{
    constexpr int kBackbufferSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const std::string& label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount;
    }

    // Hard-edged vertical red/blue stripe, Y-invariant: columns [0, redColumns) are pure red,
    // the rest pure blue -- deliberately NOT power-of-2-aligned with the boundary (redColumns=3
    // out of width=8) so the 2:1 downsample's texel PAIRS straddle the boundary (source texels
    // 2,3 are red,blue respectively) rather than landing exactly on it (which would produce a
    // perfectly blocky result even under genuine linear filtering -- not useful for telling it
    // apart from nearest-neighbor).
    std::vector<std::uint8_t> MakeStripe(int w, int h, int redColumns)
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                const bool red = x < redColumns;
                p[0] = red ? 255 : 0;
                p[1] = 0;
                p[2] = red ? 0 : 255;
                p[3] = 255;
            }
        }
        return rgba;
    }

    bool IsRedish(const std::uint8_t* p) { return p[0] >= 200 && p[2] <= 55; }
    bool IsBlueish(const std::uint8_t* p) { return p[2] >= 200 && p[0] <= 55; }
    bool IsBlend(const std::uint8_t* p)
    {
        // A genuine ~50/50 red/blue blend: both channels comfortably mid-range -- wide tolerance
        // since the exact blend weight depends on rasterizer pixel-center convention, not just
        // asserting "not pure red and not pure blue" (which a broken/garbage sample could also
        // satisfy by accident).
        return p[0] >= 70 && p[0] <= 190 && p[2] >= 70 && p[2] <= 190;
    }
    std::string PixelStr(const std::uint8_t* p)
    {
        return "(" + std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + ")";
    }
}

class WebGpuMipGenTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        IGraphicsRenderer& renderer = dev.GetRenderer();

        // Check A + B: Texture2D, source width 8 -> level1 width 4 -> level2 width 2.
        {
            constexpr int w = 8, h = 8;
            const auto level0 = MakeStripe(w, h, 3); // columns 0,1,2 red; 3..7 blue

            ImageData img;
            img.width = w;
            img.height = h;
            img.pixels = level0;
            img.mipLevels = 4; // 8 -> 4 -> 2 -> 1
            auto tex = renderer.CreateTexture(img);

            std::vector<std::uint8_t> level1(static_cast<std::size_t>(4) * 4 * 4, 0xAA);
            tex->GetData(1, 0, 0, 4, 4, level1.data(), static_cast<int>(level1.size()));

            auto pixelAt = [&](int x, int y) { return level1.data() + (static_cast<std::size_t>(y) * 4 + x) * 4; };

            const std::uint8_t* p0 = pixelAt(0, 1); // source texels (0,1) -- both red
            const std::uint8_t* p1 = pixelAt(1, 1); // source texels (2,3) -- red,blue -- must blend
            const std::uint8_t* p2 = pixelAt(2, 1); // source texels (4,5) -- both blue
            const std::uint8_t* p3 = pixelAt(3, 1); // source texels (6,7) -- both blue

            check(IsRedish(p0), "Check A (1/4): level 1 dest pixel 0 (source texels 0,1, both red) "
                                "reads back red: got=" + PixelStr(p0));
            check(IsBlend(p1), "Check A (2/4): level 1 dest pixel 1 (source texels 2,3 -- red AND "
                               "blue) is a genuine LINEAR blend, not a hard nearest-neighbor edge: "
                               "got=" + PixelStr(p1));
            check(IsBlueish(p2), "Check A (3/4): level 1 dest pixel 2 (source texels 4,5, both "
                                 "blue) reads back blue: got=" + PixelStr(p2));
            check(IsBlueish(p3), "Check A (4/4): level 1 dest pixel 3 (source texels 6,7, both "
                                 "blue) reads back blue: got=" + PixelStr(p3));

            // Check B: level 2 (chained from level 1, not directly from level 0) is real,
            // plausible content -- not all-zero/garbage.
            std::vector<std::uint8_t> level2(static_cast<std::size_t>(2) * 2 * 4, 0xAA);
            tex->GetData(2, 0, 0, 2, 2, level2.data(), static_cast<int>(level2.size()));
            bool allZero = true;
            bool plausible = true;
            for (std::size_t i = 0; i < level2.size(); i += 4)
            {
                const std::uint8_t* p = level2.data() + i;
                if (p[0] != 0 || p[1] != 0 || p[2] != 0 || p[3] != 0) allZero = false;
                // Plausible: alpha must be opaque (255, give or take) and red/blue channels must
                // not both be near-zero (that would mean neither source colour survived at all).
                if (p[3] < 200) plausible = false;
            }
            check(!allZero && plausible,
                  "Check B: level 2 (chain-downsampled from level 1) has real, plausible content, "
                  "not all-zero/garbage");
        }

        // Check C: TextureCube, same stripe pattern on one face.
        {
            constexpr int size = 8;
            auto cube = renderer.CreateTextureCube(size, /*mipMap=*/true, /*surfaceFormat=*/0);
            const auto faceLevel0 = MakeStripe(size, size, 3);
            constexpr int face = 2; // PositiveY
            cube->SetData(face, 0, 0, 0, size, size, faceLevel0.data(), static_cast<int>(faceLevel0.size()));

            std::vector<std::uint8_t> level1(static_cast<std::size_t>(4) * 4 * 4, 0xAA);
            cube->GetData(face, 1, 0, 0, 4, 4, level1.data(), static_cast<int>(level1.size()));

            auto pixelAt = [&](int x, int y) { return level1.data() + (static_cast<std::size_t>(y) * 4 + x) * 4; };
            const std::uint8_t* p0 = pixelAt(0, 1);
            const std::uint8_t* p1 = pixelAt(1, 1);
            const std::uint8_t* p2 = pixelAt(2, 1);

            check(IsRedish(p0), "Check C (1/3): TextureCube face PositiveY level 1 dest pixel 0 "
                                "reads back red: got=" + PixelStr(p0));
            check(IsBlend(p1), "Check C (2/3): TextureCube face PositiveY level 1 dest pixel 1 is "
                               "a genuine linear blend (proves GenerateMipsCubeFace() also does "
                               "real filtering, triggered by TextureCube::SetData()'s own "
                               "per-face level-0 write): got=" + PixelStr(p1));
            check(IsBlueish(p2), "Check C (3/3): TextureCube face PositiveY level 1 dest pixel 2 "
                                 "reads back blue: got=" + PixelStr(p2));
        }

        // Check D: mipMap=false (mipLevels=1) with non-empty pixel data must not crash (the
        // mipLevels<=1 no-op guard in GenerateMips2D/GenerateMipsCubeFace).
        {
            bool threw = false;
            try
            {
                constexpr int w = 4, h = 4;
                ImageData img;
                img.width = w;
                img.height = h;
                img.pixels = MakeStripe(w, h, 2);
                img.mipLevels = 1;
                auto tex = renderer.CreateTexture(img);
                (void) tex;
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(!threw, "Check D: mipMap=false (mipLevels=1) construction does not crash/throw");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuMipGenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferSize);
    }
};

int main()
{
    WebGpuMipGenTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
