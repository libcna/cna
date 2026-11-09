// SPDX-License-Identifier: MS-PL
//
// HND-TQ-47 R1: XNA's lazily cleared back buffer.
//
// Oracle: Microsoft XNA 4.0 Refresh, Microsoft.Xna.Framework.Graphics.dll 4.0.30901.0
// (~/deps/xna-il-cache/Microsoft.Xna.Framework.Graphics.il). The device keeps one lazyClearFlags
// word (Target 1, DepthBuffer 2, Stencil 4) for the surfaces it currently renders to:
//  - InitializeDeviceState IL_0097-009e (construction, and Reset IL_00e5-00ee/IL_028d) and Present
//    IL_009e-00a5 (after the native present) set it to DefaultClearOptions (get_DefaultClearOptions
//    IL_0000-0037: Target, plus DepthBuffer and Stencil when the depth format has them).
//  - ClearDirtyBuffers performs it. For the back buffer (IL_001d-0036) that is the public
//    Clear(lazyClearFlags, Color(68,34,136,255), 1, 0), and IL_0119-011b zero the word.
//  - VerifyCanDraw IL_0000-0009 (every draw, and SpriteBatch through DrawIndexedPrimitives),
//    GetBackBufferData IL_0066-006f (after its RT-active refusal, before the rectangle and element
//    checks) and Present IL_003e-0047 (before the native present) call it when the word is non-zero.
//  - Clear IL_020f-0219 removes the aspects it cleared: lazyClearFlags &= ~options.
//  - GetBackBufferData IL_01da-01ee re-arms it after a successful read when the back buffer's
//    RenderTargetUsage is DiscardContents (0); Preserve/PlatformContents keep the picture.
//  - SetRenderTargets IL_0173-0183 saves the back buffer's word when leaving it; returning to it
//    (IL_0260-027d) makes it 7 under DiscardContents or restores the saved word otherwise, and
//    IL_0539-0547 masks it with DefaultClearOptions.
// A read after Present without a draw therefore reads Color(68,34,136,255) across the viewport.
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "System/ArgumentException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kDiscard(68, 34, 136, 255);   // ClearDirtyBuffers IL_0000-0010
    const Color kRed(200, 10, 20, 255);
    constexpr int kWidth = 8;
    constexpr int kHeight = 6;

    PresentationParameters Parameters(RenderTargetUsage usage)
    {
        auto p = PresentationParameters::CreateWindowedEXT(800, 480);
        p.setBackBufferWidthProperty(kWidth);
        p.setBackBufferHeightProperty(kHeight);
        p.setBackBufferFormatProperty(SurfaceFormat::Color);
        p.setDepthStencilFormatProperty(DepthFormat::None);
        p.setMultiSampleCountProperty(0);
        p.setRenderTargetUsageProperty(usage);
        return p;
    }

    class BackBufferLazyClearRefreshTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // Renderers whose back-buffer readback returns rendered pixels.
            CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software,
                CNA::GraphicsRendererType::OpenGL33, CNA::GraphicsRendererType::OpenGLES3,
                CNA::GraphicsRendererType::WebGPU, CNA::GraphicsRendererType::SdlGpu,
                CNA::GraphicsRendererType::Fna3d);
        }

        [[nodiscard]] static std::vector<Color> Read(GraphicsDevice& device)
        {
            std::vector<Color> pixels(static_cast<std::size_t>(kWidth * kHeight));
            device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        }

        static void ExpectEverywhere(const std::vector<Color>& pixels, const Color& expected,
                                     const char* what)
        {
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                ASSERT_EQ(pixels[i], expected) << what << " at pixel " << i;
            }
        }

        // One white pixel at (0, 0), drawn without a Clear.
        static void DrawCornerPixel(GraphicsDevice& device)
        {
            Texture2D white(device, 1, 1);
            const Color solid = Color::White;
            white.SetData(&solid, 1);
            SpriteBatch sprites(device);
            sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &SamplerState::PointClamp,
                          &DepthStencilState::None, &RasterizerState::CullNone);
            sprites.Draw(white, Vector2(0.0f, 0.0f), Color::White);
            sprites.End();
        }
    };
}

TEST_F(BackBufferLazyClearRefreshTest, AFreshDeviceReadsTheDiscardColor)
{
    // InitializeDeviceState IL_0097-009e arms the clear; GetBackBufferData IL_0066-006f performs it.
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    ExpectEverywhere(Read(device), kDiscard, "first read of a new device");
}

TEST_F(BackBufferLazyClearRefreshTest, AReadAfterPresentWithoutADrawIsTheDiscardColor)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    device.Clear(kRed);
    ExpectEverywhere(Read(device), kRed, "read before Present");
    // Present IL_009e-00a5 re-arms after the native present, whatever the usage.
    device.Present();
    ExpectEverywhere(Read(device), kDiscard, "read after Present");
}

TEST_F(BackBufferLazyClearRefreshTest, ASecondReadUnderDiscardContentsIsTheDiscardColor)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::DiscardContents));
    device.Clear(kRed);
    ExpectEverywhere(Read(device), kRed, "first read");
    // IL_01da-01ee: a DiscardContents back buffer is discarded by a successful read.
    ExpectEverywhere(Read(device), kDiscard, "second read");
}

TEST_F(BackBufferLazyClearRefreshTest, RepeatedReadsUnderPreserveContentsKeepThePicture)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    device.Clear(kRed);
    ExpectEverywhere(Read(device), kRed, "first read");
    ExpectEverywhere(Read(device), kRed, "second read");
}

TEST_F(BackBufferLazyClearRefreshTest, ARefusedReadDoesNotDiscardThePicture)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::DiscardContents));
    device.Clear(kRed);
    std::vector<Color> pixels(4);
    const Rectangle outside(kWidth, 0, 2, 2);
    // The rectangle refusal is inside the read's try (IL_00b7-00da); IL_01da-01ee is not reached.
    EXPECT_THROW(device.GetBackBufferData(&outside, pixels.data(), 0, 4), System::ArgumentException);
    ExpectEverywhere(Read(device), kRed, "read after a refused read");
}

TEST_F(BackBufferLazyClearRefreshTest, AClearAfterPresentReplacesTheDiscard)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    device.Present();
    // Clear IL_020f-0219 removes Target from the pending word, so nothing purple is left to apply.
    device.Clear(kRed);
    ExpectEverywhere(Read(device), kRed, "read after Present then Clear");
}

TEST_F(BackBufferLazyClearRefreshTest, ADrawAfterPresentStartsFromTheDiscardColor)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    device.Clear(kRed);
    device.Present();
    // VerifyCanDraw IL_0000-0009 clears before the sprite batch's draw lands.
    DrawCornerPixel(device);
    const std::vector<Color> pixels = Read(device);
    EXPECT_EQ(pixels[0], Color::White);
    for (std::size_t i = 1; i < pixels.size(); ++i)
        ASSERT_EQ(pixels[i], kDiscard) << "pixel " << i;
}

TEST_F(BackBufferLazyClearRefreshTest, ADeviceDrawAfterPresentStartsFromTheDiscardColor)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D draws";
    device.Clear(kRed);
    device.Present();
    // A clip-space triangle over the top-left corner only; every DrawPrimitives-family entry
    // reaches VerifyCanDraw IL_0000-0009 before it draws.
    const Color green(0, 255, 0, 255);
    const std::array<VertexPositionColor, 3> triangle{
        VertexPositionColor(Microsoft::Xna::Framework::Vector3(-1.0f, 1.0f, 0.0f), green),
        VertexPositionColor(Microsoft::Xna::Framework::Vector3(0.0f, 1.0f, 0.0f), green),
        VertexPositionColor(Microsoft::Xna::Framework::Vector3(-1.0f, 0.0f, 0.0f), green)};
    BasicEffect effect(device);
    effect.setVertexColorEnabledProperty(true);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    const std::vector<Color> pixels = Read(device);
    EXPECT_EQ(pixels.front(), green);
    EXPECT_EQ(pixels.back(), kDiscard);
}

TEST_F(BackBufferLazyClearRefreshTest, LeavingADiscardContentsBackBufferForATargetDiscardsIt)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::DiscardContents));
    RenderTarget2D target(device, 2, 2);
    device.Clear(kRed);
    device.SetRenderTarget(&target);
    device.SetRenderTarget(nullptr);
    // SetRenderTargets IL_0260-026f: back to a DiscardContents back buffer, all aspects pending.
    ExpectEverywhere(Read(device), kDiscard, "read after a render-target round trip");
}

TEST_F(BackBufferLazyClearRefreshTest, APreserveContentsBackBufferSurvivesATargetRoundTrip)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    RenderTarget2D target(device, 2, 2);
    device.Clear(kRed);
    device.SetRenderTarget(&target);
    device.Clear(Color::Blue);
    device.SetRenderTarget(nullptr);
    // IL_0276-027d restore the saved (empty) word.
    ExpectEverywhere(Read(device), kRed, "read after a render-target round trip");
}

TEST_F(BackBufferLazyClearRefreshTest, APendingClearIsSavedAcrossATargetRoundTrip)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    RenderTarget2D target(device, 2, 2);
    device.Clear(kRed);
    device.Present();
    // IL_017c-0183 save the armed word; a Clear on the target does not touch it.
    device.SetRenderTarget(&target);
    device.Clear(Color::Blue);
    device.SetRenderTarget(nullptr);
    ExpectEverywhere(Read(device), kDiscard, "read after Present and a render-target round trip");
}

TEST_F(BackBufferLazyClearRefreshTest, ResetRearmsTheClear)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          Parameters(RenderTargetUsage::PreserveContents));
    device.Clear(kRed);
    ExpectEverywhere(Read(device), kRed, "read before Reset");
    // Reset IL_00e5-00ee and InitializeDeviceState (IL_028d) arm it again.
    device.Reset(Parameters(RenderTargetUsage::PreserveContents));
    ExpectEverywhere(Read(device), kDiscard, "read after Reset");
}
