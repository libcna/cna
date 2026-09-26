// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_DIRECTX12) && defined(CNA_CNAEXT)

#include <gtest/gtest.h>

#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "EngineTestSupport.hpp"

#include <array>
#include <cstdint>

using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferCpuAccess;
using CNA::Graphics::StorageBufferDescriptor;
using CNA::Graphics::StorageBufferUsage;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;

TEST(D3D12StorageBufferTest, AQueuedGpuCopyPreservesOddByteRangesAndWaitsOnlyForReadback)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());

    constexpr auto usage = StorageBufferUsage::TransferSource |
                           StorageBufferUsage::TransferDestination;
    constexpr auto cpu = StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write;
    StorageBuffer source(device, StorageBufferDescriptor(4096, usage, cpu));
    StorageBuffer destination(device, StorageBufferDescriptor(4096, usage, cpu));

    std::array<std::uint8_t, 79> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::uint8_t>((i * 37 + 11) & 0xFF);
    const auto waitsBefore = renderer.GetGpuWaitCountEXT();
    const auto submissionsBefore = renderer.GetFrameSubmissionCountEXT();
    source.setBytes(13, payload.data(), payload.size());
    source.copyTo(destination, 13, 211, payload.size());
    EXPECT_EQ(renderer.GetGpuWaitCountEXT(), waitsBefore);
    EXPECT_EQ(renderer.GetFrameSubmissionCountEXT(), submissionsBefore);

    std::array<std::uint8_t, 79> copied{};
    destination.getBytes(211, copied.data(), copied.size());
    EXPECT_EQ(copied, payload);
    EXPECT_GT(renderer.GetFrameSubmissionCountEXT(), submissionsBefore);

    source.copyTo(source, 13, 211, payload.size());
    std::array<std::uint8_t, 79> within{};
    source.getBytes(211, within.data(), within.size());
    EXPECT_EQ(within, payload);
}

TEST(D3D12StorageBufferTest, ThousandsOfQueuedUploadsAndCopiesKeepTheirLastValue)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    constexpr auto usage = StorageBufferUsage::TransferSource |
                           StorageBufferUsage::TransferDestination;
    constexpr auto cpu = StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write;
    StorageBuffer source(device, StorageBufferDescriptor(1024, usage, cpu));
    StorageBuffer destination(device, StorageBufferDescriptor(1024, usage, cpu));

    std::array<std::uint8_t, 1024> payload{};
    const auto waitsBefore = renderer.GetGpuWaitCountEXT();
    const auto submissionsBefore = renderer.GetFrameSubmissionCountEXT();
    for (std::uint32_t round = 0; round < 4096; ++round)
    {
        for (std::size_t byte = 0; byte < payload.size(); ++byte)
            payload[byte] = static_cast<std::uint8_t>((round + byte * 13) & 0xFF);
        source.setBytes(payload.data(), payload.size());
        source.copyTo(destination, 0, 0, payload.size());
    }
    EXPECT_EQ(renderer.GetGpuWaitCountEXT(), waitsBefore);
    EXPECT_EQ(renderer.GetFrameSubmissionCountEXT(), submissionsBefore);

    std::array<std::uint8_t, 1024> actual{};
    destination.getBytes(actual.data(), actual.size());
    EXPECT_EQ(actual, payload);
}

TEST(D3D12StorageBufferTest, CpuWrittenContentsSurviveDeviceRecreation)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    constexpr auto usage = StorageBufferUsage::TransferSource |
                           StorageBufferUsage::TransferDestination;
    constexpr auto cpu = StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write;
    StorageBuffer buffer(device, StorageBufferDescriptor(512, usage, cpu));
    const std::array<std::uint8_t, 13> expected{
        11, 37, 5, 201, 9, 71, 58, 2, 4, 64, 255, 93, 17};
    buffer.setBytes(67, expected.data(), expected.size());

    renderer.RecreateDeviceEXT();

    std::array<std::uint8_t, 13> actual{};
    buffer.getBytes(67, actual.data(), actual.size());
    EXPECT_EQ(actual, expected);
}

#endif // CNA_RENDERER_DIRECTX12 && CNA_CNAEXT
