// SPDX-License-Identifier: MS-PL
// GAP-15: linear mip generation belongs to render-target unbind, not ordinary SetData.
// XNA Refresh GraphicsDevice.ResolveRenderTarget IL_00e4..0181 loops over destination levels
// and downsamples with D3DTEXF_LINEAR (2). Keep the WEBGPU-52 stripe/filter witness here,
// on RenderTarget2D and RenderTargetCube; WebGPUAuthoredMipTests covers ordinary textures.
//
// For each row of an 8-wide stripe, source columns 0..2 are red and 3..7 blue.
// Level 1 is [red, (128,0,128), blue, blue], allowing one UNORM rounding unit.
// Level 2 is [(191,0,64), blue], again within one unit after the second quantization.
// Level 3 is (96,0,159), within one unit. Every alpha stays exactly 255.
// D checks exact single-level upload/readback as well as successful construction.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

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

    bool IsRedish(const std::uint8_t* p) { return p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255; }
    bool IsBlueish(const std::uint8_t* p) { return p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255; }
    bool IsBlend(const std::uint8_t* p)
    {
        // (255+0)/2=127.5, stored as 127 or 128 by RGBA8 UNORM quantization.
        return p[0] >= 127 && p[0] <= 128 && p[1] == 0 &&
               p[2] >= 127 && p[2] <= 128 && p[3] == 255;
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

            Texture2D source(dev, w, h, false, SurfaceFormat::Color);
            source.SetData(level0.data(), static_cast<int>(level0.size()));
            RenderTarget2D target(dev, w, h, true, SurfaceFormat::Color, DepthFormat::None,
                                  0, RenderTargetUsage::PreserveContents);
            SpriteBatch batch(dev);
            dev.SetRenderTarget(&target);
            batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &SamplerState::PointClamp, nullptr, nullptr, nullptr);
            batch.Draw(source, Rectangle(0, 0, w, h), Color::White);
            batch.End();
            dev.SetRenderTarget(nullptr); // regeneration must occur on unbind
            auto* tex = &target.GetRenderer();

            std::vector<std::uint8_t> level1(static_cast<std::size_t>(4) * 4 * 4, 0xAA);
            check(tex->GetData(1, 0, 0, 4, 4, level1.data(), static_cast<int>(level1.size())),
                  "Check A: level 1 GPU readback succeeds");

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

            // Check B: exact chained values, stronger than the former plausible/nonzero test.
            std::vector<std::uint8_t> level2(2 * 2 * 4, 0xAA);
            check(tex->GetData(2, 0, 0, 2, 2, level2.data(), static_cast<int>(level2.size())),
                  "Check B: level 2 GPU readback succeeds");
            for (int y = 0; y < 2; ++y)
            {
                const auto* p = level2.data() + y * 2 * 4;
                check(p[0] >= 190 && p[0] <= 192 && p[1] == 0 &&
                      p[2] >= 63 && p[2] <= 65 && p[3] == 255,
                      "Check B: level 2 left is (191,0,64,255) within one UNORM unit");
                check(IsBlueish(p + 4), "Check B: level 2 right stays exact blue");
            }
            std::vector<std::uint8_t> level3(4, 0xAA);
            check(tex->GetData(3, 0, 0, 1, 1, level3.data(), 4),
                  "Check B: level 3 GPU readback succeeds");
            check(level3[0] >= 95 && level3[0] <= 97 && level3[1] == 0 &&
                  level3[2] >= 158 && level3[2] <= 160 && level3[3] == 255,
                  "Check B: level 3 is (96,0,159,255) within one UNORM unit");
        }

        // Check C: TextureCube, same stripe pattern on one face.
        {
            constexpr int size = 8;
            const auto faceLevel0 = MakeStripe(size, size, 3);
            Texture2D source(dev, size, size, false, SurfaceFormat::Color);
            source.SetData(faceLevel0.data(), static_cast<int>(faceLevel0.size()));
            RenderTargetCube target(dev, size, true, SurfaceFormat::Color, DepthFormat::None,
                                    0, RenderTargetUsage::PreserveContents);
            SpriteBatch batch(dev);
            dev.SetRenderTarget(&target, CubeMapFace::PositiveY);
            batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &SamplerState::PointClamp, nullptr, nullptr, nullptr);
            batch.Draw(source, Rectangle(0, 0, size, size), Color::White);
            batch.End();
            dev.SetRenderTarget(nullptr);
            auto* cube = &target.GetRenderer();
            constexpr int face = 2; // PositiveY

            std::vector<std::uint8_t> level1(static_cast<std::size_t>(4) * 4 * 4, 0xAA);
            check(cube->GetData(face, 1, 0, 0, 4, 4, level1.data(), static_cast<int>(level1.size())),
                  "Check C: face level 1 GPU readback succeeds");

            auto pixelAt = [&](int x, int y) { return level1.data() + (static_cast<std::size_t>(y) * 4 + x) * 4; };
            const std::uint8_t* p0 = pixelAt(0, 1);
            const std::uint8_t* p1 = pixelAt(1, 1);
            const std::uint8_t* p2 = pixelAt(2, 1);

            check(IsRedish(p0), "Check C (1/3): TextureCube face PositiveY level 1 dest pixel 0 "
                                "reads back red: got=" + PixelStr(p0));
            check(IsBlend(p1), "Check C (2/3): TextureCube face PositiveY level 1 dest pixel 1 is "
                               "a genuine linear blend after render-target face unbind: got=" + PixelStr(p1));
            check(IsBlueish(p2), "Check C (3/3): TextureCube face PositiveY level 1 dest pixel 2 "
                                 "reads back blue: got=" + PixelStr(p2));
        }

        // A second pass must replace the previously generated chain, for both target kinds.
        {
            RenderTarget2D target(dev, 8, 8, true, SurfaceFormat::Color, DepthFormat::None,
                                  0, RenderTargetUsage::PreserveContents);
            RenderTargetCube cube(dev, 8, true, SurfaceFormat::Color, DepthFormat::None,
                                  0, RenderTargetUsage::PreserveContents);
            for (const Color color : {Color::Red, Color::Blue})
            {
                dev.SetRenderTarget(&target);
                dev.Clear(color);
                dev.SetRenderTarget(nullptr);
                dev.SetRenderTarget(&cube, CubeMapFace::NegativeZ);
                dev.Clear(color);
                dev.SetRenderTarget(nullptr);
                for (int level = 0; level < 4; ++level)
                {
                    const int edge = 8 >> level;
                    std::vector<Color> pixels(edge * edge, Color::Transparent);
                    target.GetData(level, nullptr, pixels.data(), 0, edge * edge);
                    for (const auto& pixel : pixels)
                        check(pixel == color, "2D render-target unbind refreshes every level");
                    cube.GetData(CubeMapFace::NegativeZ, level, nullptr, pixels.data(), 0, edge * edge);
                    for (const auto& pixel : pixels)
                        check(pixel == color, "cube render-target unbind refreshes every level");
                }
            }
        }

        // Check D: a non-mipped ordinary texture still stores its uploaded pixels exactly.
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
                std::vector<std::uint8_t> actual(img.pixels.size(), 0xAA);
                check(tex->GetData(0, 0, 0, w, h, actual.data(), static_cast<int>(actual.size())),
                      "Check D: single-level GPU readback succeeds");
                check(actual == img.pixels, "Check D: single-level upload stores exact stripe bytes");
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
    return (totalCount > 0 && passCount == totalCount) ? 0 : 1;
}
