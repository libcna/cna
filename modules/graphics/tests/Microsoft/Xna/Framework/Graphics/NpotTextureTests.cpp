// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-187: non-power-of-two Texture2D sizes upload, mip, sample and read
// back correctly.
//
// Renderer-neutral on purpose. NPOT is not a WebGPU-only concern -- `easygl_npot_texture_test` is
// the reference these mirror.
//
// WHICH HALF REACHES WHICH CODE, because writing this file found that the two are not the same and
// the obvious reading is wrong. `Texture2D::GetData` for a plain texture is served entirely from
// the framework's own CPU shadow (`Texture2D.cpp`: `gpuOnlyContent_` is false, so the renderer's
// `GetData` is never called, and with the shadow freed it throws rather than reading the GPU). So
// the `Texture2D` cases below prove that SetData/GetData handle odd sizes -- worth having, and
// renderer-neutral -- but they cannot reach a renderer's readback at all, and a fixture that
// claimed they did would be measuring the wrong thing. Verified: breaking WebGPU's texture
// readback stride left every one of them green.
//
// The renderer's readback is reachable only through a RENDER TARGET, which is `gpuOnlyContent_` and
// therefore asks the renderer. That is where the staging alignment lives: on WebGPU a readback
// stages through a buffer whose rows are padded to 256 bytes, so a 13-texel RGBA row is 52 bytes
// carrying 204 bytes of padding the readback must strip, while at a width whose row already divides
// 256 the padding is zero and a path that forgot to strip it looks perfect. The render-target cases
// at the end of this file are therefore given in pairs -- one width that needs padding, one that
// does not -- so "it works" and "it works for the reason we think" stay separable.
//
// Every texel carries a value derived from its own coordinates, so a readback that returned the
// right NUMBER of texels from the wrong OFFSETS -- which is exactly what a mis-stripped row stride
// produces -- fails on content rather than on size.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <string>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    /// A texel whose every channel encodes its own coordinates, so a readback from the wrong offset
    /// is a wrong COLOUR rather than a missing one. The +1s keep 0 out of the alpha channel, which
    /// some paths treat as "nothing here".
    [[nodiscard]] Color TexelAt(int x, int y)
    {
        return Color(static_cast<SharpRuntime::bytecs>(10 + x * 7),
                     static_cast<SharpRuntime::bytecs>(20 + y * 11),
                     static_cast<SharpRuntime::bytecs>(30 + x * 3 + y * 5),
                     255);
    }

    [[nodiscard]] std::vector<Color> MakePattern(int width, int height)
    {
        std::vector<Color> texels(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                texels[static_cast<std::size_t>(y * width + x)] = TexelAt(x, y);
        return texels;
    }

    /// XNA's own mip rule: halve, floor, never below 1, until 1x1.
    [[nodiscard]] int ExpectedLevelCount(int width, int height)
    {
        int levels = 1;
        while (width > 1 || height > 1)
        {
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
            ++levels;
        }
        return levels;
    }

    void ExpectRoundTrip(GraphicsDevice& device, int width, int height, const char* why)
    {
        Texture2D texture(device, width, height, false, SurfaceFormat::Color);
        const std::vector<Color> written = MakePattern(width, height);
        texture.SetData(written.data(), static_cast<int>(written.size()));

        std::vector<Color> read(written.size(), Color(0, 0, 0, 0));
        texture.GetData(read.data(), static_cast<int>(read.size()));

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                EXPECT_EQ(read[index].getPackedValueProperty(),
                          written[index].getPackedValueProperty())
                    << why << " -- texel (" << x << "," << y << ") of a " << width << "x" << height
                    << " texture came back wrong, which is what a mis-stripped staging row stride "
                       "produces";
                if (read[index].getPackedValueProperty() != written[index].getPackedValueProperty())
                    return;   // one wrong texel is the finding; do not print the whole texture
            }
        }
    }
}

// The Texture2D half: odd sizes survive SetData/GetData. See the header for why this exercises the
// framework's shadow rather than any renderer's readback.
TEST(NpotTexture, AnOddWidthRoundTripsExactly)
{
    GraphicsDevice device;
    ExpectRoundTrip(device, 13, 7, "an odd width");
}

TEST(NpotTexture, AnAlreadyAlignedWidthRoundTripsExactly)
{
    GraphicsDevice device;
    ExpectRoundTrip(device, 64, 3, "a width whose row is already a 256-byte multiple");
}

TEST(NpotTexture, AWidthOfOneRoundTripsExactly)
{
    GraphicsDevice device;
    ExpectRoundTrip(device, 1, 5, "a single-texel width");
}

TEST(NpotTexture, APrimeSizedTextureRoundTripsExactly)
{
    GraphicsDevice device;
    ExpectRoundTrip(device, 17, 11, "a prime width and height");
}

// A sub-rectangle at an offset that divides nothing: the readback has to honour BOTH the source
// rectangle and the staging stride, and getting either wrong lands on a different texel.
TEST(NpotTexture, ASubRectangleOfAnOddTextureReadsTheRightTexels)
{
    GraphicsDevice device;
    constexpr int kWidth = 13;
    constexpr int kHeight = 7;
    Texture2D texture(device, kWidth, kHeight, false, SurfaceFormat::Color);
    const std::vector<Color> written = MakePattern(kWidth, kHeight);
    texture.SetData(written.data(), static_cast<int>(written.size()));

    const Rectangle region(3, 2, 7, 4);
    std::vector<Color> read(static_cast<std::size_t>(region.Width) * region.Height,
                            Color(0, 0, 0, 0));
    texture.GetData(0, &region, read.data(), 0, static_cast<int>(read.size()));

    for (int y = 0; y < region.Height; ++y)
    {
        for (int x = 0; x < region.Width; ++x)
        {
            const Color expected = TexelAt(region.X + x, region.Y + y);
            EXPECT_EQ(read[static_cast<std::size_t>(y * region.Width + x)].getPackedValueProperty(),
                      expected.getPackedValueProperty())
                << "sub-rectangle texel (" << x << "," << y << ") -- a readback that honoured the "
                   "rectangle but not the stride, or the stride but not the rectangle, lands here";
        }
    }
}

// Mip counts for sizes that never divide evenly. XNA floors, so 13x7 walks 13x7, 6x3, 3x1, 1x1.
TEST(NpotTexture, MipLevelCountsFollowXnasFlooringRule)
{
    GraphicsDevice device;
    const struct { int width; int height; } kSizes[] = {
        {13, 7}, {17, 11}, {1, 5}, {64, 3}, {3, 3}, {1, 1},
    };
    for (const auto& size : kSizes)
    {
        Texture2D texture(device, size.width, size.height, true, SurfaceFormat::Color);
        EXPECT_EQ(texture.getLevelCountProperty(), ExpectedLevelCount(size.width, size.height))
            << size.width << "x" << size.height
            << ": XNA halves and FLOORS, so an odd dimension loses its remainder rather than "
               "rounding up";
    }
}

// A mip-mapped NPOT texture still round-trips its level 0, which is where the framework and the
// renderer have to agree about the level's own dimensions rather than the base's.
TEST(NpotTexture, AMipMappedOddTextureRoundTripsItsBaseLevel)
{
    GraphicsDevice device;
    constexpr int kWidth = 13;
    constexpr int kHeight = 7;
    Texture2D texture(device, kWidth, kHeight, true, SurfaceFormat::Color);
    ASSERT_GT(texture.getLevelCountProperty(), 1);

    const std::vector<Color> written = MakePattern(kWidth, kHeight);
    texture.SetData(written.data(), static_cast<int>(written.size()));

    std::vector<Color> read(written.size(), Color(0, 0, 0, 0));
    texture.GetData(read.data(), static_cast<int>(read.size()));
    for (std::size_t index = 0; index < written.size(); ++index)
    {
        EXPECT_EQ(read[index].getPackedValueProperty(), written[index].getPackedValueProperty())
            << "texel " << index << " of a mip-mapped 13x7 texture's base level";
        if (read[index].getPackedValueProperty() != written[index].getPackedValueProperty()) return;
    }
}

// --- The render-target half: the only path that reaches a renderer's readback ----------------

namespace
{
    /// Clears a NPOT render target to a known colour and reads it back through the RENDERER.
    void ExpectTargetReadback(GraphicsDevice& device, int width, int height, const char* why)
    {
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
        using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;

        // A colour with four distinct channels, so a byte-order or stride slip is a wrong value
        // rather than a plausible one.
        const Color painted(0x21, 0x43, 0x65, 0xFF);
        RenderTarget2D target(device, width, height, false, SurfaceFormat::Color, DepthFormat::None,
                              0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&target);
        device.Clear(painted);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> read(static_cast<std::size_t>(width) * height, Color(0, 0, 0, 0));
        target.GetData(read.data(), static_cast<int>(read.size()));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                EXPECT_EQ(read[index].getPackedValueProperty(), painted.getPackedValueProperty())
                    << why << " -- texel (" << x << "," << y << ") of a " << width << "x" << height
                    << " render target. A readback that walked the staging buffer with a tight row "
                       "stride instead of the padded one lands on the wrong row here";
                if (read[index].getPackedValueProperty() != painted.getPackedValueProperty())
                    return;
            }
        }
    }
}

TEST(NpotTexture, AnOddWidthRenderTargetReadsBackThroughTheRenderer)
{
    GraphicsDevice device;
    // 13 * 4 = 52 bytes: on WebGPU the staging row is padded to 256 and 204 bytes must be stripped.
    ExpectTargetReadback(device, 13, 7, "an odd width whose staging row needs padding");
}

TEST(NpotTexture, AnAlreadyAlignedRenderTargetReadsBackThroughTheRenderer)
{
    GraphicsDevice device;
    // 64 * 4 = 256 bytes exactly: no padding at all, so this passes even for a renderer that never
    // strips it. It is the control that gives the previous test its meaning.
    ExpectTargetReadback(device, 64, 3, "a width whose staging row is already aligned");
}

TEST(NpotTexture, ASingleTexelWideRenderTargetReadsBackThroughTheRenderer)
{
    GraphicsDevice device;
    // The extreme: a 4-byte row against a 256-byte alignment, 98% padding.
    ExpectTargetReadback(device, 1, 5, "a single-texel-wide render target");
}
