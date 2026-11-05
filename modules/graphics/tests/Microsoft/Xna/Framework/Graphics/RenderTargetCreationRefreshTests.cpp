// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class RenderTargetCreationRefreshTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
        }
        static void Expect2D(const RenderTarget2D& target, int levels, DepthFormat depth,
                             int samples, RenderTargetUsage usage)
        {
            EXPECT_EQ(target.getWidthProperty(), 8);
            EXPECT_EQ(target.getHeightProperty(), 4);
            EXPECT_EQ(target.getLevelCountProperty(), levels);
            EXPECT_EQ(target.getFormatProperty(), SurfaceFormat::Color);
            EXPECT_EQ(target.getDepthStencilFormatProperty(), depth);
            EXPECT_EQ(target.getMultiSampleCountProperty(), samples);
            EXPECT_EQ(target.getRenderTargetUsageProperty(), usage);
        }
    };
}

TEST_F(RenderTargetCreationRefreshTest, TwoDimensionalConstructorDefaultsAndFullArguments)
{
    GraphicsDevice device;
    // Refresh RT2D .ctor(device,w,h) IL_000a-000f supplies false, Color, None, 0, Discard.
    RenderTarget2D defaults(device, 8, 4);
    Expect2D(defaults, 1, DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    // Six-argument .ctor IL_0010-0012 supplies only samples=0 and usage=Discard.
    RenderTarget2D six(device, 8, 4, true, SurfaceFormat::Color, DepthFormat::Depth16);
    Expect2D(six, 4, DepthFormat::Depth16, 0, RenderTargetUsage::DiscardContents);
    // Full .ctor IL_000a-0014 forwards all arguments; mip sizes: 8x4,4x2,2x1,1x1.
    RenderTarget2D full(device, 8, 4, true, SurfaceFormat::Color,
                        DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PlatformContents);
    Expect2D(full, 4, DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PlatformContents);
}

TEST_F(RenderTargetCreationRefreshTest, CubeConstructorDefaultsAndFullArguments)
{
    GraphicsDevice device;
    // Refresh RTCube five-argument .ctor IL_000e-0010 supplies samples=0, usage=Discard.
    RenderTargetCube defaults(device, 8, false, SurfaceFormat::Color, DepthFormat::Depth16);
    EXPECT_EQ(defaults.getSizeProperty(), 8);
    EXPECT_EQ(defaults.getWidthProperty(), 8);
    EXPECT_EQ(defaults.getHeightProperty(), 8);
    EXPECT_EQ(defaults.getLevelCountProperty(), 1);
    EXPECT_EQ(defaults.getFormatProperty(), SurfaceFormat::Color);
    EXPECT_EQ(defaults.getDepthStencilFormatProperty(), DepthFormat::Depth16);
    EXPECT_EQ(defaults.getMultiSampleCountProperty(), 0);
    EXPECT_EQ(defaults.getRenderTargetUsageProperty(), RenderTargetUsage::DiscardContents);
    // Full .ctor IL_0009-0012 forwards all arguments. Chain: 8,4,2,1 -> four levels.
    RenderTargetCube full(device, 8, true, SurfaceFormat::Color,
                          DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
    EXPECT_EQ(full.getSizeProperty(), 8);
    EXPECT_EQ(full.getLevelCountProperty(), 4);
    EXPECT_EQ(full.getFormatProperty(), SurfaceFormat::Color);
    EXPECT_EQ(full.getDepthStencilFormatProperty(), DepthFormat::Depth24Stencil8);
    EXPECT_EQ(full.getMultiSampleCountProperty(), 4);
    EXPECT_EQ(full.getRenderTargetUsageProperty(), RenderTargetUsage::PreserveContents);
}

TEST_F(RenderTargetCreationRefreshTest, SampleNegotiationReportsZeroOrFourRatherThanTheRequest)
{
    GraphicsDevice device;
    // Refresh QueryMultiSampleCount IL_0000-000d maps <=1 to 0 and caps at 16;
    // IL_0035-0046 searches downward. SOFTWARE offers only 4 (SoftwareRendererDescriptor.cpp
    // ClampMultiSampleCount; SoftwareFramebuffer::SetMultiSampleCount). Thus 2/3 -> 0,
    // and >=4 -> 4. These literals are not observations copied from a CNA run.
    const std::array requests{-7, 0, 1, 2, 3, 4, 5, 7, 16, 17, std::numeric_limits<int>::max()};
    const std::array expected{0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4};
    for (std::size_t i = 0; i != requests.size(); ++i)
    {
        SCOPED_TRACE(requests[i]);
        RenderTarget2D flat(device, 4, 4, false, SurfaceFormat::Color, DepthFormat::None,
                            requests[i], RenderTargetUsage::PreserveContents);
        RenderTargetCube cube(device, 4, false, SurfaceFormat::Color, DepthFormat::None,
                              requests[i], RenderTargetUsage::PlatformContents);
        EXPECT_EQ(flat.getMultiSampleCountProperty(), expected[i]);
        EXPECT_EQ(cube.getMultiSampleCountProperty(), expected[i]);
    }
}

TEST_F(RenderTargetCreationRefreshTest, HiDefNpotAndDegenerateMipChainsHaveExactLength)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // RT CreateRenderTarget passes mipMap to Texture.CreateTexture (RT2D IL_006a-0073,
    // RTCube IL_0067-006e). Hand-derived floor-halving chains end when all axes reach 1.
    for (const auto shape : {std::array{1, 1, 1}, std::array{1, 9, 4},
                             std::array{9, 1, 4}, std::array{3, 5, 3}})
    {
        SCOPED_TRACE(shape[0]);
        SCOPED_TRACE(shape[1]);
        RenderTarget2D target(device, shape[0], shape[1], true,
                              SurfaceFormat::Color, DepthFormat::None);
        EXPECT_EQ(target.getLevelCountProperty(), shape[2]);
    }
    RenderTargetCube cube(device, 9, true, SurfaceFormat::Color, DepthFormat::None);
    EXPECT_EQ(cube.getLevelCountProperty(), 4); // 9 -> 4 -> 2 -> 1.
}

TEST_F(RenderTargetCreationRefreshTest, PreferredCompressedFormatFallsBackWithoutLosingOtherOptions)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // Refresh RT2D/RTCube.CreateRenderTarget IL_0026-0037 ignores QueryFormat's false
    // exact-match result and uses its selected values. DXT1 is not renderable; Color is
    // the SOFTWARE fallback. Mip/usage/depth must still survive that substitution.
    RenderTarget2D flat(device, 8, 4, true, SurfaceFormat::Dxt1, DepthFormat::Depth16,
                        4, RenderTargetUsage::PlatformContents);
    Expect2D(flat, 4, DepthFormat::Depth16, 4, RenderTargetUsage::PlatformContents);
    RenderTargetCube cube(device, 8, true, SurfaceFormat::Dxt1, DepthFormat::Depth16,
                          4, RenderTargetUsage::PreserveContents);
    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Color);
    EXPECT_EQ(cube.getLevelCountProperty(), 4);
    EXPECT_EQ(cube.getDepthStencilFormatProperty(), DepthFormat::Depth16);
    EXPECT_EQ(cube.getMultiSampleCountProperty(), 4);
    EXPECT_EQ(cube.getRenderTargetUsageProperty(), RenderTargetUsage::PreserveContents);
}
