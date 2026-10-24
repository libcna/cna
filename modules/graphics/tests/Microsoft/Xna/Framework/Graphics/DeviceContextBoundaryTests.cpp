// SPDX-License-Identifier: MS-PL
// ISSUE-086/112: device-local binding and teardown while another device owns the thread.
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

std::unique_ptr<GraphicsDevice> MakeDevice()
{
    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
    parameters.setBackBufferWidthProperty(64);
    parameters.setBackBufferHeightProperty(64);
    parameters.setDepthStencilFormatProperty(DepthFormat::None);
    parameters.setPresentationIntervalProperty(PresentInterval::Immediate);
    return std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                                           GraphicsProfile::HiDef, parameters);
}

Color BackBufferPixel(GraphicsDevice& device)
{
    const Rectangle origin(0, 0, 1, 1);
    Color pixel = Color::Transparent;
    device.GetBackBufferData(&origin, &pixel, 0, 1);
    return pixel;
}

Color TargetPixel(RenderTarget2D& target)
{
    const Rectangle origin(0, 0, 1, 1);
    Color pixel = Color::Transparent;
    target.GetData(0, &origin, &pixel, 0, 1);
    return pixel;
}

class DeviceContextBoundaryTest : public testing::Test
{
protected:
    std::unique_ptr<GraphicsDevice> primary;
    void SetUp() override
    {
        primary = MakeDevice();
        RecordProperty("renderer", std::string(primary->GetGraphicsRendererName()));
    }
};

TEST_F(DeviceContextBoundaryTest, Secondary2DBindingUsesItsOwningContext)
{
    auto secondary = MakeDevice();
    RenderTarget2D target(*secondary, 20, 10, false, SurfaceFormat::Color,
                          DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    // Model the primary Game frame without reaching private GraphicsDevice state.
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    primary->Clear(Color::CornflowerBlue);
    secondary->SetRenderTarget(&target);
    secondary->Clear(Color::LimeGreen);
    secondary->SetRenderTarget(nullptr);
    EXPECT_EQ(TargetPixel(target), Color::LimeGreen);
    EXPECT_EQ(BackBufferPixel(*primary), Color::CornflowerBlue);
}

TEST_F(DeviceContextBoundaryTest, SecondaryCubeBindingUsesItsOwningContext)
{
    auto secondary = MakeDevice();
    RenderTargetCube target(*secondary, 8, false, SurfaceFormat::Color,
                            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    primary->Clear(Color::CornflowerBlue);
    secondary->SetRenderTarget(&target, CubeMapFace::NegativeZ);
    secondary->Clear(Color::Orange);
    secondary->SetRenderTarget(nullptr);
    const Rectangle origin(0, 0, 1, 1);
    Color pixel = Color::Transparent;
    target.GetData(CubeMapFace::NegativeZ, 0, &origin, &pixel, 0, 1);
    EXPECT_EQ(pixel, Color::Orange);
    EXPECT_EQ(BackBufferPixel(*primary), Color::CornflowerBlue);
}

TEST_F(DeviceContextBoundaryTest, SecondaryMrtBindingUsesItsOwningContext)
{
    auto secondary = MakeDevice();
    if (secondary->GetRenderer().GetMaxRenderTargetsForProfileEXT(1) < 2)
        GTEST_SKIP() << "Renderer does not support two HiDef render targets";
    RenderTarget2D first(*secondary, 16, 8, false, SurfaceFormat::Color,
                         DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    RenderTarget2D second(*secondary, 16, 8, false, SurfaceFormat::Color,
                          DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    primary->Clear(Color::CornflowerBlue);
    secondary->SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)});
    secondary->Clear(Color::Magenta);
    secondary->SetRenderTargets({});
    EXPECT_EQ(TargetPixel(first), Color::Magenta);
    EXPECT_EQ(TargetPixel(second), Color::Magenta);
    EXPECT_EQ(BackBufferPixel(*primary), Color::CornflowerBlue);
}

TEST_F(DeviceContextBoundaryTest, SecondaryUnbindRestoresItsOwnBackbuffer)
{
    auto secondary = MakeDevice();
    RenderTarget2D target(*secondary, 20, 10, false, SurfaceFormat::Color,
                          DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    {
        // Correctly establish the initial binding independently of ISSUE-112.
        auto setup = secondary->GetRenderer().AcquireThreadContextLeaseEXT();
        secondary->SetRenderTarget(&target);
        secondary->Clear(Color::LimeGreen);
    }
    secondary->SetRenderTarget(nullptr);
    secondary->Clear(Color::Orange);
    EXPECT_EQ(TargetPixel(target), Color::LimeGreen);
    EXPECT_EQ(BackBufferPixel(*secondary), Color::Orange);
}

TEST_F(DeviceContextBoundaryTest, SecondaryDisposePreservesThePrimaryFramebuffer)
{
    auto secondary = MakeDevice();
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    primary->Clear(Color::LimeGreen);
    ASSERT_EQ(BackBufferPixel(*primary), Color::LimeGreen);
    secondary->Dispose();
    primary->Clear(Color::Orange);
    EXPECT_EQ(BackBufferPixel(*primary), Color::Orange);
    EXPECT_FALSE(primary->getIsDisposedProperty());
    EXPECT_TRUE(secondary->getIsDisposedProperty());
}

TEST_F(DeviceContextBoundaryTest, SecondaryColorReadbackUsesItsOwningContext)
{
    auto secondary = MakeDevice();
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    primary->Clear(Color::CornflowerBlue);
    secondary->Clear(Color::Orange);
    std::vector<Color> pixels(64 * 64 + 1, Color::Transparent);
    secondary->GetBackBufferData(pixels.data(), 64 * 64);
    EXPECT_EQ(pixels.front(), Color::Orange);
    EXPECT_EQ(pixels[64 * 64 - 1], Color::Orange);
    pixels.front() = Color::Transparent;
    secondary->GetBackBufferData(pixels.data(), 1, 64 * 64);
    EXPECT_EQ(pixels.front(), Color::Transparent);
    EXPECT_EQ(pixels.back(), Color::Orange);
    EXPECT_EQ(BackBufferPixel(*secondary), Color::Orange);
    EXPECT_EQ(BackBufferPixel(*primary), Color::CornflowerBlue);
}

TEST_F(DeviceContextBoundaryTest, SecondaryPackedReadbackUsesItsOwningContext)
{
    auto secondary = MakeDevice();
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    primary->Clear(Color::CornflowerBlue);
    secondary->Clear(Color::Orange);
    constexpr std::uint32_t orange = 0xff00a5ff;
    std::vector<std::uint32_t> pixels(64 * 64 + 1, 0);
    secondary->GetBackBufferData(pixels.data(), 64 * 64);
    EXPECT_EQ(pixels.front(), orange);
    EXPECT_EQ(pixels[64 * 64 - 1], orange);
    pixels.front() = 0;
    secondary->GetBackBufferData(pixels.data(), 1, 64 * 64);
    EXPECT_EQ(pixels.front(), 0u);
    EXPECT_EQ(pixels.back(), orange);
    const Rectangle origin(0, 0, 1, 1);
    std::uint32_t pixel = 0;
    secondary->GetBackBufferData(&origin, &pixel, 0, 1);
    EXPECT_EQ(pixel, orange);
    EXPECT_EQ(BackBufferPixel(*primary), Color::CornflowerBlue);
}

TEST_F(DeviceContextBoundaryTest, SecondaryDestructionPreservesPrimaryTextureDrawing)
{
    Texture2D texture(*primary, 1, 1);
    const Color color = Color::Magenta;
    texture.SetData(&color, 1);
    SpriteBatch batch(*primary);
    auto secondary = MakeDevice();
    auto frame = primary->GetRenderer().AcquireThreadContextLeaseEXT();
    secondary.reset();
    primary->Clear(Color::CornflowerBlue);
    batch.Begin();
    batch.Draw(texture, Rectangle(0, 0, 8, 8), Color::White);
    batch.End();
    EXPECT_EQ(BackBufferPixel(*primary), Color::Magenta);
    EXPECT_FALSE(texture.getIsDisposedProperty());
}
}
