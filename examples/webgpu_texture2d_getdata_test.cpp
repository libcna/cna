// SPDX-License-Identifier: MS-PL
// WEBGPU-51: verify WebGPUTextureRenderer::GetData() -- real CPU readback of an arbitrary
// Texture2D renderer, via the same staged MAP_READ-buffer/aligned-row/async-map-and-poll technique
// WEBGPU-91's ReadBackbuffer() and WebGPURenderTargetRenderer::GetData() (WEBGPU-53/54) already
// established. Before this task, WebGPUTextureRenderer had no GetData() override at all, silently
// falling back to ITextureRenderer's own no-op default (data left untouched).
//
// Design note: Texture2D::GetData() (the XNA-facing API) is implemented entirely via a CPU-side
// pixel shadow (cpuPixels_) maintained in the shared, renderer-agnostic Texture2D.cpp -- it only
// ever calls through to ITextureRenderer::GetData() for a RenderTarget2D-backed instance
// (gpuOnlyContent_), which on this renderer already uses the separate WebGPURenderTargetRenderer
// class (see webgpu_rendertarget2d_test.cpp). So exercising WebGPUTextureRenderer::GetData()
// itself requires going around the XNA Texture2D layer and driving IGraphicsRenderer directly --
// matching examples/webgpu_instanced3d_test.cpp's own established convention for testing a
// renderer capability with no XNA-layer round trip of its own.
//
// Check A -- full-texture round trip: upload a 4x4 RGBA8 image (16 distinct-ish pixels, a
//   gradient) via CreateTexture()+UpdatePixels(), then GetData(level=0, whole rect) must return
//   the exact same bytes.
// Check B -- sub-rectangle round trip: GetData() for a 2x2 rectangle strictly inside the texture
//   (not the top-left corner) must return exactly the 4 pixels at that offset, not the whole
//   texture or a shifted region -- proves the x/y sub-rect offset is genuinely honoured, not just
//   assumed to be (0,0).
// Check C -- mip level round trip: a texture created with mipMap=true, level 1 uploaded via
//   UpdatePixelsLevel() with distinct content from level 0, GetData(level=1, ...) must read back
//   level 1's own content, not level 0's -- proves the mip level parameter is honoured.
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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Graphics::ImageData;

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

    // A simple deterministic pixel generator so every pixel in a WxH image is distinct (not just
    // a flat colour) -- a bug that read back a shifted/garbled region would show up immediately.
    std::vector<std::uint8_t> MakeGradient(int w, int h, std::uint8_t seed)
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                p[0] = static_cast<std::uint8_t>(seed + x * 16);
                p[1] = static_cast<std::uint8_t>(seed + y * 16);
                p[2] = static_cast<std::uint8_t>(seed + x + y);
                p[3] = 255;
            }
        }
        return rgba;
    }
}

class WebGpuTexture2DGetDataTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        IGraphicsRenderer& renderer = dev.GetRenderer();

        // Check A: full 4x4 round trip.
        {
            constexpr int w = 4, h = 4;
            const auto pixels = MakeGradient(w, h, 10);

            ImageData img;
            img.width = w;
            img.height = h;
            img.pixels = pixels;
            img.mipLevels = 1;
            auto tex = renderer.CreateTexture(img);

            std::vector<std::uint8_t> readback(static_cast<std::size_t>(w) * h * 4, 0xAA);
            tex->GetData(0, 0, 0, w, h, readback.data(), static_cast<int>(readback.size()));

            const bool match = (readback == pixels);
            check(match, "Check A: full 4x4 Texture2D GetData() matches the uploaded gradient exactly");
        }

        // Check B: sub-rectangle round trip, offset strictly inside the texture.
        {
            constexpr int w = 8, h = 8;
            const auto pixels = MakeGradient(w, h, 40);

            ImageData img;
            img.width = w;
            img.height = h;
            img.pixels = pixels;
            img.mipLevels = 1;
            auto tex = renderer.CreateTexture(img);

            constexpr int rx = 3, ry = 2, rw = 2, rh = 2;
            std::vector<std::uint8_t> readback(static_cast<std::size_t>(rw) * rh * 4, 0xAA);
            tex->GetData(0, rx, ry, rw, rh, readback.data(), static_cast<int>(readback.size()));

            bool match = true;
            for (int row = 0; row < rh && match; ++row)
            {
                for (int col = 0; col < rw && match; ++col)
                {
                    const std::uint8_t* expected = pixels.data() +
                        (static_cast<std::size_t>(ry + row) * w + (rx + col)) * 4;
                    const std::uint8_t* got = readback.data() + (static_cast<std::size_t>(row) * rw + col) * 4;
                    if (std::memcmp(expected, got, 4) != 0) match = false;
                }
            }
            check(match, "Check B: 2x2 sub-rectangle GetData() at a non-origin offset (3,2) "
                        "matches exactly those source pixels, not the whole texture / a shifted region");
        }

        // Check C: mip level round trip -- level 1 uploaded with DIFFERENT content than level 0.
        {
            constexpr int w = 8, h = 8;
            const auto level0 = MakeGradient(w, h, 0);
            const auto level1 = MakeGradient(w / 2, h / 2, 200); // distinct seed

            ImageData img;
            img.width = w;
            img.height = h;
            img.pixels = level0;
            img.mipLevels = 2; // pre-allocate level 1 too
            auto tex = renderer.CreateTexture(img);
            tex->UpdatePixelsLevel(1, level1.data(), w / 2, h / 2);

            std::vector<std::uint8_t> readback(static_cast<std::size_t>(w / 2) * (h / 2) * 4, 0xAA);
            tex->GetData(1, 0, 0, w / 2, h / 2, readback.data(), static_cast<int>(readback.size()));

            const bool match = (readback == level1);
            check(match, "Check C: GetData(level=1) reads back level 1's own uploaded content, "
                        "not level 0's (proves the mip level parameter is honoured)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuTexture2DGetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuTexture2DGetDataTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
