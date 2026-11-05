// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SDLGPU) || defined(CNA_RENDERER_PRESENT_SDLGPU)
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>

TEST(SdlGpuFloatCapability, RawAndPublicQueriesDescribeExistingFloatStorage)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64,64));
    auto* renderer=dynamic_cast<SdlGpu::SdlGpuRenderer*>(&device.GetRenderer());
    if (!renderer) GTEST_SKIP() << "requires the selected SDLGPU renderer";
    for (const auto format : {SurfaceFormat::Vector4,SurfaceFormat::HdrBlendable})
    {
        const auto capability=format==SurfaceFormat::Vector4 ? CNA::GraphicsCapability::FloatRenderTargets :
                                                               CNA::GraphicsCapability::HalfFloatRenderTargets;
        // CNAEXT capability contract: public GraphicsDevice uses these representative formats;
        // SDL support was implemented in 8df965818/e236a8d33, and is queried per live device.
        const bool supported=renderer->ClassifyRenderTargetFormatEXT(static_cast<int>(format))==
            RendererFormatVerdict::Supported;
        EXPECT_EQ(renderer->SupportsCapability(capability),supported);
        EXPECT_EQ(device.SupportsCapability(capability),supported);
        if (!supported) continue;
        RenderTarget2D target(device,1,1,false,format,DepthFormat::None);
        ASSERT_EQ(target.getFormatProperty(),format);
        device.SetRenderTarget(&target); device.Clear(-2,3,0.5f,0.25f);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // Independent IEEE storage bits for -2,3,1/2,1/4; no CNA half conversion oracle.
        std::array<std::uint32_t,4> full{};
        std::array<std::uint16_t,4> half{};
        if (format==SurfaceFormat::Vector4)
        {
            ASSERT_TRUE(target.GetRenderTargetRenderer()->GetData(0,0,0,1,1,full.data(),sizeof(full)));
            const std::array<std::uint32_t,4> expected{0xc0000000,0x40400000,0x3f000000,0x3e800000};
            EXPECT_EQ(full,expected);
        }
        else
        {
            ASSERT_TRUE(target.GetRenderTargetRenderer()->GetData(0,0,0,1,1,half.data(),sizeof(half)));
            const std::array<std::uint16_t,4> expected{0xc000,0x4200,0x3800,0x3400};
            EXPECT_EQ(half,expected);
        }
    }
}
#endif
