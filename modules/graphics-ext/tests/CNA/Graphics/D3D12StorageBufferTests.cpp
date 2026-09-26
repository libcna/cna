// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_DIRECTX12) && defined(CNA_CNAEXT)

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "EngineTestSupport.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using CNA::Graphics::ComputeShader;
using CNA::Graphics::StorageBuffer;
using CNA::Graphics::StorageBufferT;
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

TEST(D3D12ComputeRecoveryTest, ProgramAndStorageBindingSurviveDeviceRecreation)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    StorageBufferT<float> values(device, 64);
    values.setData(std::vector<float>(64, 1.0f));
    ComputeShader shader(device, R"(
RWByteAddressBuffer Values : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint at = id.x * 4;
    Values.Store(at, asuint(asfloat(Values.Load(at)) * 2.0f));
}
)");
    shader.bindStorageBuffer(0, values.getBuffer());
    shader.dispatch(1);
    for (float value : values.getData()) EXPECT_FLOAT_EQ(value, 2.0f);

    renderer.RecreateDeviceEXT();

    values.setData(std::vector<float>(64, 3.0f));
    shader.dispatch(1);
    for (float value : values.getData()) EXPECT_FLOAT_EQ(value, 6.0f);
}

TEST(D3D12ComputeRecoveryTest, AnUncompiledProgramDoesNotPreventDeviceRecreation)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    auto broken = renderer.CreateComputeShader("this is not an HLSL shader\n");
    ASSERT_NE(broken, nullptr);
    EXPECT_FALSE(broken->IsValid());
    EXPECT_FALSE(broken->GetCompileError().empty());

    EXPECT_NO_THROW(renderer.RecreateDeviceEXT());
    EXPECT_FALSE(broken->IsValid());
    EXPECT_FALSE(broken->GetCompileError().empty());
}

TEST(D3D12ComputeBindingTest, SimultaneousWritableAliasesAreRejectedBeforeDispatch)
{
    CnaTest::EngineLayer::HiDefDevice device;
    StorageBuffer buffer(device, StorageBufferDescriptor(
        256, StorageBufferUsage::Storage | StorageBufferUsage::Constant,
        StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write));
    const std::string source = R"(
RWByteAddressBuffer Values : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Values.Store(0, 42); }
)";
    ComputeShader shader(device, source);
    shader.bindStorageBuffer(0, buffer);
    shader.bindStorageBuffer(1, buffer);
    EXPECT_THROW(shader.dispatch(1), std::invalid_argument);

    ComputeShader constantAlias(device, source);
    constantAlias.bindStorageBuffer(0, buffer);
    constantAlias.bindConstantBuffer(0, buffer);
    EXPECT_THROW(constantAlias.dispatch(1), std::invalid_argument);
}

TEST(D3D12ComputeBindingTest, ForeignRendererRecordsAreRejectedBeforeGpuRecording)
{
    CnaTest::EngineLayer::HiDefDevice firstDevice;
    CnaTest::EngineLayer::HiDefDevice secondDevice;
    auto& first = dynamic_cast<DirectX12Renderer&>(firstDevice.GetRenderer());
    auto& second = dynamic_cast<DirectX12Renderer&>(secondDevice.GetRenderer());
    const std::string source = R"(
RWByteAddressBuffer Values : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Values.Store(0, 42); }
)";
    auto program = first.CreateComputeShader(source);
    auto foreignProgram = second.CreateComputeShader(source);
    ASSERT_TRUE(program->IsValid());
    ASSERT_TRUE(foreignProgram->IsValid());
    std::shared_ptr<CNA::Internal::Renderers::IStorageBufferRenderer> foreign(
        second.CreateStorageBufferEXT(256, UINT32_C(0x47), UINT32_C(0x03)));
    std::shared_ptr<CNA::Internal::Renderers::IStorageBufferRenderer> local(
        first.CreateStorageBufferEXT(256, UINT32_C(0x47), UINT32_C(0x03)));
    ASSERT_NE(foreign, nullptr);
    ASSERT_NE(local, nullptr);

    EXPECT_THROW(program->BindStorageBuffer(0, foreign.get()), std::invalid_argument);
    EXPECT_FALSE(program->BindConstantBufferEXT(0, foreign.get()));
    EXPECT_THROW(first.BindStorageBufferForDrawEXT(0, *foreign),
                 System::NotSupportedException);
    EXPECT_THROW(first.DispatchCompute(foreignProgram.get(), 1, 1, 1),
                 std::invalid_argument);
    EXPECT_FALSE(foreign->CopyToEXT(*local, 0, 0, 4));
}

#endif // CNA_RENDERER_DIRECTX12 && CNA_CNAEXT
