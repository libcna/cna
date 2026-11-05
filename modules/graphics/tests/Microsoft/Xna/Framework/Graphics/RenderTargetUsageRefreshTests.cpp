// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Color;

namespace
{
    class RenderTargetUsageRefreshTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
        }
        static void ExpectEveryPixel(const std::vector<Color>& pixels, const Color& expected)
        {
            for (std::size_t i = 0; i != pixels.size(); ++i)
                EXPECT_EQ(pixels[i].getPackedValueProperty(), expected.getPackedValueProperty()) << i;
        }
    };
    constexpr std::array usages{RenderTargetUsage::DiscardContents,
        RenderTargetUsage::PreserveContents, RenderTargetUsage::PlatformContents};
}

TEST_F(RenderTargetUsageRefreshTest, Every2DUsageHasAnExactOutcomeAfterRebinding)
{
    GraphicsDevice device;
    const Color painted(33, 67, 101, 255);
    // Refresh SetRenderTargets IL_0393-03b8 marks only DiscardContents for lazy clear;
    // ClearDirtyBuffers IL_0000-0010 spells RGBA=(68,34,136,255), IL_005c-0063 tests usage.
    // Preserve and Platform skip that clear in this shipped Windows Refresh assembly.
    for (const auto usage : usages)
    {
        SCOPED_TRACE(static_cast<int>(usage));
        RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, usage);
        device.SetRenderTarget(&target);
        device.Clear(painted);
        device.SetRenderTargets({});
        device.SetRenderTarget(&target);
        device.SetRenderTargets({});
        std::vector<Color> pixels(16, Color(9, 8, 7, 6));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        ExpectEveryPixel(pixels, usage == RenderTargetUsage::DiscardContents
            ? Color(68, 34, 136, 255) : painted);
    }
}

TEST_F(RenderTargetUsageRefreshTest, EveryCubeUsagePreservesFaceIdentityAcrossRebinding)
{
    GraphicsDevice device;
    const Color first(33, 67, 101, 255), second(177, 83, 49, 255);
    // Same Refresh usage/clear oracle as the 2D test; selected cube face is carried through
    // SetRenderTargets IL_0332-0337 and ClearDirtyBuffers IL_0068-007b.
    for (const auto usage : usages)
    {
        SCOPED_TRACE(static_cast<int>(usage));
        RenderTargetCube cube(device, 4, false, SurfaceFormat::Color, DepthFormat::None, 0, usage);
        device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        device.Clear(first);
        device.SetRenderTarget(&cube, CubeMapFace::NegativeZ);
        device.Clear(second);
        device.SetRenderTargets({});
        device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        device.SetRenderTargets({});
        std::vector<Color> pixels(16, Color(9, 8, 7, 6));
        cube.GetData(CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
        ExpectEveryPixel(pixels, usage == RenderTargetUsage::DiscardContents
            ? Color(68, 34, 136, 255) : first);
        cube.GetData(CubeMapFace::NegativeZ, pixels.data(), static_cast<int>(pixels.size()));
        ExpectEveryPixel(pixels, second); // Rebinding +X must not clear -Z.
    }
}

TEST_F(RenderTargetUsageRefreshTest, EqualDiscardBindingDoesNotDiscardTwice)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 4, 4); // DiscardContents.
    const Color painted(33, 67, 101, 255);
    device.SetRenderTarget(&target);
    device.Clear(painted);
    // Refresh SetRenderTargets IL_0015-0083 returns before discard for an equal binding.
    device.SetRenderTargets({RenderTargetBinding(&target)});
    device.SetRenderTargets({});
    std::vector<Color> pixels(16, Color(9, 8, 7, 6));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    ExpectEveryPixel(pixels, painted);
}
