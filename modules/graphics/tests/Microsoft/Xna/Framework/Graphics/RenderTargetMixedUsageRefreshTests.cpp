// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/RendererTestGate.hpp"
#include "../../../../../examples/conformance/RenderTargetMixedUsageFixture.hpp"
#if defined(CNA_RENDERER_SOFTWARE)
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#endif

using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    class RenderTargetMixedUsageRefreshTest : public testing::Test
    {
    protected:
        void SetUp() override { CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software); }
        static void Check(bool ok,const std::string& label) { EXPECT_TRUE(ok) << label; }
    };
}
TEST_F(RenderTargetMixedUsageRefreshTest, EveryOrderedFourSlotUsageSet)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::FourSlots(device,Check);
}
TEST_F(RenderTargetMixedUsageRefreshTest, MixedCubeFacesInEverySlot)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::CubeFaces(device,Check);
}
TEST_F(RenderTargetMixedUsageRefreshTest, PublicClearAndEqualBindingRemainWholeSetAndNoOp)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::PublicClearAndEqualBinding(device,Check);
}
TEST_F(RenderTargetMixedUsageRefreshTest, SlotZeroOwnsDepthAndStencil)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::DepthStencilOwnership(device,Check);
}
TEST_F(RenderTargetMixedUsageRefreshTest, FourSampleStorageKeepsPerSlotDiscardAndCubeFaceIdentity)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::FourSlots(device,Check,4);
    CNA::Tests::MixedUsage::CubeFaces(device,Check,4);
}
TEST_F(RenderTargetMixedUsageRefreshTest, CubeAndTwoDimensionalBindingsShareSlotZeroDepthRule)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::CubeDepthStencilOwnership(device,Check);
}
TEST_F(RenderTargetMixedUsageRefreshTest, DiscardIgnoresWriteMasksAndRetainsCallerState)
{
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CNA::Tests::MixedUsage::MaskedState(device,Check);
}
#if defined(CNA_RENDERER_SOFTWARE)
TEST_F(RenderTargetMixedUsageRefreshTest, SecondaryDepthAndStencilAreNeverDiscardedByTheSet)
{
    using Microsoft::Xna::Framework::Color;
    using CNA::Internal::Renderers::Software::SoftwareRenderTargetRenderer;
    GraphicsDevice device; device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    // Refresh SetRenderTargets IL_0370-0392 binds only slot 0's depth; IL_03ad-03b8
    // requests depth/stencil clearing only for discard slot 0. A preserving secondary has
    // distinct storage: Refresh may alias depth ONLY between compatible Discard targets
    // (RenderTargetHelper.CreateSurfaces IL_0073-0092, GetSharedDepthSurface IL_0027-0087).
    for(auto firstUsage : CNA::Tests::MixedUsage::usages)
    for(auto secondUsage : {RenderTargetUsage::PreserveContents,RenderTargetUsage::PlatformContents})
    for(auto depthFormat : {DepthFormat::None,DepthFormat::Depth16,DepthFormat::Depth24Stencil8})
    {
        RenderTarget2D a(device,4,4,false,SurfaceFormat::Color,depthFormat,0,firstUsage);
        RenderTarget2D b(device,4,4,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,0,secondUsage);
        auto* ar=dynamic_cast<SoftwareRenderTargetRenderer*>(a.GetRenderTargetRenderer());
        auto* br=dynamic_cast<SoftwareRenderTargetRenderer*>(b.GetRenderTargetRenderer());
        ASSERT_NE(ar,nullptr); ASSERT_NE(br,nullptr);
        // Seed concrete storage independently, including a secondary depth that never binds.
        ar->Framebuffer().ClearDepthValue(0.25f); ar->Framebuffer().ClearStencilValue(9);
        br->Framebuffer().ClearDepthValue(0.75f); br->Framebuffer().ClearStencilValue(13);
        device.SetRenderTargets({RenderTargetBinding(&a),RenderTargetBinding(&b)});
        ASSERT_EQ(ar->Framebuffer().depthBuffer.size(),depthFormat==DepthFormat::None ? 0u : 16u);
        ASSERT_EQ(ar->Framebuffer().stencilBuffer.size(),depthFormat==DepthFormat::Depth24Stencil8 ? 16u : 0u);
        for(float z : ar->Framebuffer().depthBuffer)
            EXPECT_FLOAT_EQ(z,firstUsage==RenderTargetUsage::DiscardContents ? 1.0f : 0.25f);
        for(auto stencil : ar->Framebuffer().stencilBuffer)
            EXPECT_EQ(stencil,firstUsage==RenderTargetUsage::DiscardContents ? 0 : 9);
        ASSERT_EQ(br->Framebuffer().depthBuffer.size(),16u);
        ASSERT_EQ(br->Framebuffer().stencilBuffer.size(),16u);
        for(float z : br->Framebuffer().depthBuffer) EXPECT_FLOAT_EQ(z,0.75f);
        for(auto stencil : br->Framebuffer().stencilBuffer) EXPECT_EQ(stencil,13);
        device.SetRenderTargets({});
    }
}
#endif
