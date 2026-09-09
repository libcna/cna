// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-185: partial-rectangle and partial-box transfers, at level 0 and
// above, on every resource kind that has them.
//
// Renderer-neutral, mirroring EasyGL's `texture2d_partial_rect` / `texture2d_mip` /
// `texturecube_{partial_rect,mip,faces}` / `texture3d_{partial_box,partial_box_readback,mip,slices}`.
// The renderer code accepts sub-regions already, so this is verification rather than
// implementation -- and verification of a specific shape: **the region written is the only region
// that changed.** A partial transfer that quietly rewrote the whole level, or that landed at the
// origin instead of the requested offset, produces a texture in which the sub-region is right and
// everything around it is wrong, so every test here checks the surroundings as carefully as the
// target.
//
// Every offset is deliberately non-origin and every extent deliberately smaller than the level, so
// an implementation that ignored either lands somewhere this notices. The patterns encode
// coordinates rather than being flat, for the same reason: a sub-region written at the wrong offset
// from a flat source is invisible.
//
// THE RENDER-TARGET CASE IS THE ONE WITH A REAL CODE PATH BEHIND IT. `Texture2D::SetData` takes a
// read-modify-write route through `renderer_->GetData()` when a partial level-0 update arrives and
// there is no CPU shadow to merge into -- and for a plain `Texture2D` that situation THROWS by
// design ("partial update requires CPU-side pixel storage"), so the route is reachable only through
// a render target, whose content lives on the GPU and nowhere else. Writing a sub-rectangle into a
// target that was RENDERED into is therefore the only way to exercise it: the untouched texels must
// still hold what was drawn, not the transparent black a fabricated shadow would supply.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    /// A background texel that encodes its own coordinates, so a wrongly placed write is a wrong
    /// colour rather than a missing one.
    [[nodiscard]] Color Background(int x, int y, int z = 0)
    {
        return Color(static_cast<SharpRuntime::bytecs>(10 + x * 9),
                     static_cast<SharpRuntime::bytecs>(20 + y * 13),
                     static_cast<SharpRuntime::bytecs>(30 + z * 17), 255);
    }

    /// The patch texel, from a palette that cannot collide with any Background value in the sizes
    /// used here: the red channel starts at 200 where Background's tops out well below it.
    [[nodiscard]] Color Patch(int x, int y, int z = 0)
    {
        return Color(static_cast<SharpRuntime::bytecs>(200 + x * 5),
                     static_cast<SharpRuntime::bytecs>(120 + y * 7),
                     static_cast<SharpRuntime::bytecs>(60 + z * 11), 255);
    }

    [[nodiscard]] std::vector<Color> FillBackground(int width, int height)
    {
        std::vector<Color> texels(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                texels[static_cast<std::size_t>(y * width + x)] = Background(x, y);
        return texels;
    }
}

// A non-origin sub-rectangle at level 0: inside it the patch, outside it the untouched background.
TEST(PartialTextureTransfer, ATexture2DSubRectangleChangesOnlyItself)
{
    GraphicsDevice device;
    constexpr int kWidth = 8;
    constexpr int kHeight = 6;
    Texture2D texture(device, kWidth, kHeight, false, SurfaceFormat::Color);
    const std::vector<Color> background = FillBackground(kWidth, kHeight);
    texture.SetData(background.data(), static_cast<int>(background.size()));

    const Rectangle region(3, 2, 4, 3);
    std::vector<Color> patch(static_cast<std::size_t>(region.Width) * region.Height);
    for (int y = 0; y < region.Height; ++y)
        for (int x = 0; x < region.Width; ++x)
            patch[static_cast<std::size_t>(y * region.Width + x)] = Patch(x, y);
    texture.SetData(0, &region, patch.data(), 0, static_cast<int>(patch.size()));

    std::vector<Color> read(background.size());
    texture.GetData(read.data(), static_cast<int>(read.size()));
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const bool inside = x >= region.X && x < region.X + region.Width && y >= region.Y &&
                                y < region.Y + region.Height;
            const Color expected = inside ? Patch(x - region.X, y - region.Y) : Background(x, y);
            EXPECT_EQ(read[static_cast<std::size_t>(y * kWidth + x)].getPackedValueProperty(),
                      expected.getPackedValueProperty())
                << "texel (" << x << "," << y << ") is " << (inside ? "inside" : "OUTSIDE")
                << " the written rectangle -- a partial write that rewrote the whole level, or that "
                   "landed at the origin, fails here";
        }
    }
}

// The same claim one mip level up, where the level's own dimensions -- not the base's -- are what
// the offset and extent are measured against.
TEST(PartialTextureTransfer, ATexture2DSubRectangleOnMipLevelOne)
{
    GraphicsDevice device;
    constexpr int kWidth = 8;
    constexpr int kHeight = 8;
    Texture2D texture(device, kWidth, kHeight, true, SurfaceFormat::Color);
    ASSERT_GT(texture.getLevelCountProperty(), 1);
    const std::vector<Color> base = FillBackground(kWidth, kHeight);
    texture.SetData(base.data(), static_cast<int>(base.size()));

    constexpr int kLevelW = kWidth / 2;
    constexpr int kLevelH = kHeight / 2;
    const std::vector<Color> levelBackground = FillBackground(kLevelW, kLevelH);
    texture.SetData(1, nullptr, levelBackground.data(), 0,
                    static_cast<int>(levelBackground.size()));

    const Rectangle region(1, 1, 2, 2);
    std::array<Color, 4> patch{Patch(0, 0), Patch(1, 0), Patch(0, 1), Patch(1, 1)};
    texture.SetData(1, &region, patch.data(), 0, static_cast<int>(patch.size()));

    std::vector<Color> read(levelBackground.size());
    texture.GetData(1, nullptr, read.data(), 0, static_cast<int>(read.size()));
    for (int y = 0; y < kLevelH; ++y)
    {
        for (int x = 0; x < kLevelW; ++x)
        {
            const bool inside = x >= region.X && x < region.X + region.Width && y >= region.Y &&
                                y < region.Y + region.Height;
            const Color expected = inside ? Patch(x - region.X, y - region.Y) : Background(x, y);
            EXPECT_EQ(read[static_cast<std::size_t>(y * kLevelW + x)].getPackedValueProperty(),
                      expected.getPackedValueProperty())
                << "level-1 texel (" << x << "," << y << ") -- an implementation measuring the "
                   "rectangle against the BASE level's size lands elsewhere";
        }
    }
}

// Six faces, one sub-rectangle: the other five faces must not move. A cube that shared one
// allocation without offsetting by face writes the patch into all of them.
TEST(PartialTextureTransfer, ACubeFaceSubRectangleLeavesTheOtherFivesAlone)
{
    GraphicsDevice device;
    constexpr int kSize = 8;
    constexpr std::array<CubeMapFace, 6> kFaces{
        CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
        CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};

    TextureCube cube(device, kSize, false, SurfaceFormat::Color);
    // Each face starts as a DIFFERENT background, so "face 2 changed" and "every face changed" are
    // distinguishable, and so is "the write went to the wrong face".
    for (int face = 0; face < 6; ++face)
    {
        std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize);
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
                texels[static_cast<std::size_t>(y * kSize + x)] = Background(x, y, face);
        cube.SetData(kFaces[static_cast<std::size_t>(face)], texels.data(),
                     static_cast<int>(texels.size()));
    }

    constexpr int kTargetFace = 2;
    const Rectangle region(2, 3, 3, 2);
    std::vector<Color> patch(static_cast<std::size_t>(region.Width) * region.Height);
    for (int y = 0; y < region.Height; ++y)
        for (int x = 0; x < region.Width; ++x)
            patch[static_cast<std::size_t>(y * region.Width + x)] = Patch(x, y);
    cube.SetData(kFaces[kTargetFace], 0, &region, patch.data(), 0, static_cast<int>(patch.size()));

    for (int face = 0; face < 6; ++face)
    {
        std::vector<Color> read(static_cast<std::size_t>(kSize) * kSize);
        cube.GetData(kFaces[static_cast<std::size_t>(face)], read.data(),
                     static_cast<int>(read.size()));
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const bool inside = face == kTargetFace && x >= region.X &&
                                    x < region.X + region.Width && y >= region.Y &&
                                    y < region.Y + region.Height;
                const Color expected =
                    inside ? Patch(x - region.X, y - region.Y) : Background(x, y, face);
                EXPECT_EQ(read[static_cast<std::size_t>(y * kSize + x)].getPackedValueProperty(),
                          expected.getPackedValueProperty())
                    << "face " << face << " texel (" << x << "," << y << ")";
                if (read[static_cast<std::size_t>(y * kSize + x)].getPackedValueProperty() !=
                    expected.getPackedValueProperty())
                    return;
            }
        }
    }
}

// A sub-BOX, which adds the third axis: a write that got the slice wrong lands on a different z.
TEST(PartialTextureTransfer, ATexture3DSubBoxChangesOnlyItself)
{
    GraphicsDevice device;
    constexpr int kWidth = 6;
    constexpr int kHeight = 5;
    constexpr int kDepth = 4;
    std::unique_ptr<Texture3D> volume;
    try
    {
        volume = std::make_unique<Texture3D>(device, kWidth, kHeight, kDepth, false,
                                             SurfaceFormat::Color);
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer has no Texture3D";
    }
    catch (const std::runtime_error&)
    {
        GTEST_SKIP() << "this renderer has no Texture3D";
    }

    std::vector<Color> background(static_cast<std::size_t>(kWidth) * kHeight * kDepth);
    for (int z = 0; z < kDepth; ++z)
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
                background[static_cast<std::size_t>((z * kHeight + y) * kWidth + x)] =
                    Background(x, y, z);
    volume->SetData(background.data(), static_cast<int>(background.size()));

    constexpr int kLeft = 2, kTop = 1, kRight = 5, kBottom = 4, kFront = 1, kBack = 3;
    const int boxW = kRight - kLeft, boxH = kBottom - kTop, boxD = kBack - kFront;
    std::vector<Color> patch(static_cast<std::size_t>(boxW) * boxH * boxD);
    for (int z = 0; z < boxD; ++z)
        for (int y = 0; y < boxH; ++y)
            for (int x = 0; x < boxW; ++x)
                patch[static_cast<std::size_t>((z * boxH + y) * boxW + x)] = Patch(x, y, z);
    volume->SetData(0, kLeft, kTop, kRight, kBottom, kFront, kBack, patch.data(), 0,
                    static_cast<int>(patch.size()));

    std::vector<Color> read(background.size());
    volume->GetData(read.data(), static_cast<int>(read.size()));
    for (int z = 0; z < kDepth; ++z)
    {
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const bool inside = x >= kLeft && x < kRight && y >= kTop && y < kBottom &&
                                    z >= kFront && z < kBack;
                const Color expected =
                    inside ? Patch(x - kLeft, y - kTop, z - kFront) : Background(x, y, z);
                const std::size_t index = static_cast<std::size_t>((z * kHeight + y) * kWidth + x);
                EXPECT_EQ(read[index].getPackedValueProperty(), expected.getPackedValueProperty())
                    << "voxel (" << x << "," << y << "," << z << ") is "
                    << (inside ? "inside" : "OUTSIDE") << " the written box";
                if (read[index].getPackedValueProperty() != expected.getPackedValueProperty())
                    return;
            }
        }
    }
}

// THE RENDER-TARGET HALF IS ABSENT ON PURPOSE, and this is the finding it cost.
//
// `Texture2D::SetData` takes a read-modify-write route through `renderer_->GetData()` when a
// partial level-0 update arrives with no CPU shadow to merge into, and for a plain `Texture2D` that
// situation THROWS by design ("partial update requires CPU-side pixel storage"). So the route is
// reachable only through a render target -- and writing that test showed that **`SetData` on a
// `RenderTarget2D` is silently discarded, on EasyGL and WebGPU alike, for a partial rectangle AND
// for a whole level.** Neither renderer overrides `ITextureRenderer::UpdatePixels` on its
// render-target class, and the base declares it with an empty body, so the merged pixels the shared
// layer carefully assembles are handed to a no-op. Measured: after `SetData`, the target still
// reads back what was RENDERED into it, and nothing throws.
//
// Recorded as `plans/plan_graphics.md` row 1118 rather than asserted here. A test that pinned the
// current behaviour would be asserting the defect as correct, and one that pinned the right
// behaviour would be permanently red -- so this file steers around it and names the row, the same
// way `parity_basic_effect_vertex_color` steers around row 1116. The readback HALF of that route is
// covered and does work: `NpotTexture.*RenderTarget*` reads rendered content back through the
// renderer at three sizes.
