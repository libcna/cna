// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_DIRECTX12) && defined(CNA_CNAEXT)

#include <gtest/gtest.h>

#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <memory>

using CNA::Graphics::GpuTimer;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
using Microsoft::Xna::Framework::Color;

TEST(D3D12GpuTimerTest, RecordingAndDisposalDoNotSubmitOrWaitForTheGpu)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    ASSERT_TRUE(renderer.SupportsGpuTimerEXT());
    const auto waits = renderer.GetGpuWaitCountEXT();
    const auto submissions = renderer.GetFrameSubmissionCountEXT();

    auto timer = std::make_unique<GpuTimer>(device);
    ASSERT_TRUE(timer->isSupported());
    timer->begin();
    device.Clear(Color::Blue);
    timer->end();
    timer.reset();

    EXPECT_EQ(renderer.GetGpuWaitCountEXT(), waits);
    EXPECT_EQ(renderer.GetFrameSubmissionCountEXT(), submissions);
    EXPECT_NO_THROW(device.Present());
}

TEST(D3D12GpuTimerTest, AQueuedRangeCanBeReplacedAfterDeviceRecreation)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    GpuTimer timer(device);
    ASSERT_TRUE(timer.isSupported());

    timer.begin();
    device.Clear(Color::Blue);
    timer.end();
    ASSERT_NO_THROW(renderer.RecreateDeviceEXT());
    EXPECT_TRUE(timer.poll());
    EXPECT_EQ(timer.getSampleCount(), 1);
    EXPECT_DOUBLE_EQ(timer.getLastMilliseconds(), 0.0);

    timer.begin();
    device.Clear(Color::Green);
    timer.end();
    device.Present();
    for (int attempt = 0; attempt < 10000 && timer.getSampleCount() < 2; ++attempt)
    {
        device.Clear(Color::Black);
        (void)timer.poll();
        if ((attempt % 8) == 7) device.Present();
    }
    EXPECT_EQ(timer.getSampleCount(), 2);
    EXPECT_GE(timer.getLastMilliseconds(), 0.0);
}

#endif // CNA_RENDERER_DIRECTX12 && CNA_CNAEXT
