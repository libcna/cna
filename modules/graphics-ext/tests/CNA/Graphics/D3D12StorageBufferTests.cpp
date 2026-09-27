// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_DIRECTX12) && defined(CNA_CNAEXT)

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Graphics/StorageTexture2D.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <algorithm>
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
using CNA::Graphics::StorageTexture2D;
using CNA::Graphics::StorageTexture2DDescriptor;
using CNA::Graphics::StorageTexture2DUsage;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

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

TEST(D3D12StorageTextureTest, EveryAdvertisedTypedUavFormatTransfersExactRectangles)
{
    CnaTest::EngineLayer::HiDefDevice device;
    constexpr auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination));
    int checked = 0;
    for (int ordinal = 0; ordinal <= static_cast<int>(SurfaceFormat::UShortEXT); ++ordinal)
    {
        const auto format = static_cast<SurfaceFormat>(ordinal);
        if (!device.GetRendererSurfaceFormatSupportEXT(format).Supports(required)) continue;
        SCOPED_TRACE("SurfaceFormat " + std::to_string(ordinal));
        const int bytesPerTexel =
            Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format);
        ASSERT_GT(bytesPerTexel, 0);
        StorageTexture2D image(device, StorageTexture2DDescriptor(
            4, 3, 1, format,
            StorageTexture2DUsage::StorageWrite |
                StorageTexture2DUsage::TransferSource |
                StorageTexture2DUsage::TransferDestination));
        std::vector<std::uint8_t> expected(
            static_cast<std::size_t>(4 * 3 * bytesPerTexel));
        for (std::size_t i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<std::uint8_t>(i * 37u + 11u);
        image.setData(0, nullptr, expected.data(), expected.size());
        const Microsoft::Xna::Framework::Rectangle region(1, 1, 2, 1);
        std::vector<std::uint8_t> patch(
            static_cast<std::size_t>(2 * bytesPerTexel), UINT8_C(0xA5));
        image.setData(0, &region, patch.data(), patch.size());
        std::copy(patch.begin(), patch.end(),
                  expected.begin() + static_cast<std::size_t>(5 * bytesPerTexel));
        std::vector<std::uint8_t> actual(expected.size());
        image.getData(0, nullptr, actual.data(), actual.size());
        EXPECT_EQ(actual, expected);
        std::vector<std::uint8_t> actualPatch(patch.size());
        image.getData(0, &region, actualPatch.data(), actualPatch.size());
        EXPECT_EQ(actualPatch, patch);
        ++checked;
    }
    EXPECT_TRUE(device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Color)
                    .Supports(required));
    EXPECT_GT(checked, 0);
}

TEST(D3D12StorageTextureTest, OddDimensionsKeepIndependentMipContents)
{
    CnaTest::EngineLayer::HiDefDevice device;
    constexpr auto usage = StorageTexture2DUsage::StorageWrite |
        StorageTexture2DUsage::TransferSource |
        StorageTexture2DUsage::TransferDestination;
    StorageTexture2D image(device, StorageTexture2DDescriptor(
        7, 5, 3, SurfaceFormat::Color, usage));
    std::array<std::uint8_t, 7 * 5 * 4> base{};
    std::array<std::uint8_t, 3 * 2 * 4> middle{};
    const std::array<std::uint8_t, 4> tip{211, 73, 29, 255};
    for (std::size_t i = 0; i < base.size(); ++i)
        base[i] = static_cast<std::uint8_t>(i * 13u);
    for (std::size_t i = 0; i < middle.size(); ++i)
        middle[i] = static_cast<std::uint8_t>(i * 7u + 3u);
    image.setData(0, nullptr, base.data(), base.size());
    image.setData(1, nullptr, middle.data(), middle.size());
    image.setData(2, nullptr, tip.data(), tip.size());

    const Microsoft::Xna::Framework::Rectangle right(1, 0, 2, 2);
    const std::array<std::uint8_t, 16> patch{
        9, 19, 29, 39, 49, 59, 69, 79,
        89, 99, 109, 119, 129, 139, 149, 159};
    image.setData(1, &right, patch.data(), patch.size());
    for (int row = 0; row < 2; ++row)
        std::copy_n(patch.data() + row * 8, 8, middle.data() + row * 12 + 4);

    std::array<std::uint8_t, base.size()> actualBase{};
    std::array<std::uint8_t, middle.size()> actualMiddle{};
    std::array<std::uint8_t, tip.size()> actualTip{};
    std::array<std::uint8_t, patch.size()> actualPatch{};
    image.getData(0, nullptr, actualBase.data(), actualBase.size());
    image.getData(1, nullptr, actualMiddle.data(), actualMiddle.size());
    image.getData(2, nullptr, actualTip.data(), actualTip.size());
    image.getData(1, &right, actualPatch.data(), actualPatch.size());
    EXPECT_EQ(actualBase, base);
    EXPECT_EQ(actualMiddle, middle);
    EXPECT_EQ(actualTip, tip);
    EXPECT_EQ(actualPatch, patch);
}

TEST(D3D12StorageTextureTest, TypedFloatImageFeedsComputeAndSurvivesRecovery)
{
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    constexpr auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination));
    if (!device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Single).Supports(required))
        GTEST_SKIP() << "the physical device has no typed R32_FLOAT UAV load/store";
    StorageTexture2D image(device, StorageTexture2DDescriptor(
        2, 2, 1, SurfaceFormat::Single,
        StorageTexture2DUsage::StorageRead |
            StorageTexture2DUsage::StorageWrite |
            StorageTexture2DUsage::TransferSource |
            StorageTexture2DUsage::TransferDestination));
    ComputeShader shader(device, R"(
RWTexture2D<float> Image : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Image[uint2(1, 1)] = Image[uint2(1, 1)] * 2.0f + 0.25f;
}
)");
    shader.bindStorageTexture(0, image, CNA::GraphicsImageAccess::ReadWrite);
    const std::array<float, 4> initial{0.5f, 1.25f, 2.0f, 3.5f};
    image.setData(0, nullptr, initial.data(), sizeof(initial));
    shader.dispatch(1);
    std::array<float, 4> actual{};
    image.getData(0, nullptr, actual.data(), sizeof(actual));
    EXPECT_EQ(actual[0], initial[0]);
    EXPECT_EQ(actual[1], initial[1]);
    EXPECT_EQ(actual[2], initial[2]);
    EXPECT_EQ(actual[3], 7.25f);

    renderer.RecreateDeviceEXT();
    const std::array<float, 4> afterReset{4.0f, 5.0f, 6.0f, 7.0f};
    image.setData(0, nullptr, afterReset.data(), sizeof(afterReset));
    shader.dispatch(1);
    image.getData(0, nullptr, actual.data(), sizeof(actual));
    EXPECT_EQ(actual[0], afterReset[0]);
    EXPECT_EQ(actual[1], afterReset[1]);
    EXPECT_EQ(actual[2], afterReset[2]);
    EXPECT_EQ(actual[3], 14.25f);
}

TEST(D3D12StorageTextureTest, ForeignRecordsAndDuplicateWritableAliasesAreRejected)
{
    CnaTest::EngineLayer::HiDefDevice firstDevice;
    CnaTest::EngineLayer::HiDefDevice secondDevice;
    auto& first = dynamic_cast<DirectX12Renderer&>(firstDevice.GetRenderer());
    auto& second = dynamic_cast<DirectX12Renderer&>(secondDevice.GetRenderer());
    constexpr std::uint32_t usage = UINT32_C(0x02) | UINT32_C(0x10);
    std::shared_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer> foreign(
        second.CreateStorageTexture2DEXT(2, 2, 1, static_cast<int>(SurfaceFormat::Color), usage));
    ASSERT_NE(foreign, nullptr);
    auto program = first.CreateComputeShader(R"(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Output[uint2(0, 0)] = 1.0; }
)");
    ASSERT_TRUE(program->IsValid()) << program->GetCompileError();
    EXPECT_FALSE(program->BindStorageTexture2DEXT(0, foreign, 1));

    StorageTexture2D local(firstDevice, StorageTexture2DDescriptor(
        2, 2, 1, SurfaceFormat::Color,
        StorageTexture2DUsage::StorageWrite | StorageTexture2DUsage::TransferSource));
    ComputeShader alias(firstDevice, R"(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Output[uint2(0, 0)] = 1.0; }
)");
    alias.bindStorageTexture(0, local, CNA::GraphicsImageAccess::WriteOnly);
    alias.bindStorageTexture(1, local, CNA::GraphicsImageAccess::WriteOnly);
    EXPECT_THROW(alias.dispatch(1), std::invalid_argument);
}

TEST(D3D12StorageTextureTest, AQueuedUploadCanOutliveTheGraphicsDevice)
{
    auto device = std::make_unique<CnaTest::EngineLayer::HiDefDevice>();
    auto image = std::make_unique<StorageTexture2D>(
        *device, StorageTexture2DDescriptor(
            2, 2, 1, SurfaceFormat::Color,
            StorageTexture2DUsage::StorageWrite |
                StorageTexture2DUsage::TransferDestination));
    const std::array<std::uint8_t, 16> pixels{
        1, 2, 3, 255, 4, 5, 6, 255,
        7, 8, 9, 255, 10, 11, 12, 255};
    image->setData(0, nullptr, pixels.data(), pixels.size());
    device.reset();
    EXPECT_NO_THROW(image.reset());
}

TEST(D3D12StorageTextureTest, ComputeOutputIsSampledByGraphicsInTheSameFrame)
{
    using CNA::Graphics::ShaderBindingRequirementEXT;
    using CNA::Graphics::ShaderBindingTypeEXT;
    using CNA::Graphics::ShaderCodeEXT;
    using CNA::Graphics::ShaderPackageEXT;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    CnaTest::EngineLayer::HiDefDevice device;
    StorageTexture2D image(device, StorageTexture2DDescriptor(
        1, 1, 1, SurfaceFormat::Color,
        StorageTexture2DUsage::StorageWrite | StorageTexture2DUsage::Sampled |
            StorageTexture2DUsage::TransferSource));
    {
        ComputeShader writer(device, R"(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[uint2(0, 0)] = float4(0, 1, 0, 1);
}
)");
        writer.bindStorageTexture(0, image, CNA::GraphicsImageAccess::WriteOnly);
        writer.dispatch(1);
    }

    const ShaderPackageEXT package(
        {
            ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex,
                          "main", "storage.dx12.vert.hlsl", R"(
struct Input { float3 position : POSITION; };
float4 main(Input input) : SV_POSITION
{
    return float4(input.position.xy, 0.0, 1.0);
}
)"),
            ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Fragment,
                          "main", "storage.dx12.frag.hlsl", R"(
Texture2D<float4> Source : register(t0);
float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    return Source.Load(int3(0, 0, 0));
}
)"),
        },
        {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
        {ShaderBindingRequirementEXT("Source", 0,
                                     ShaderBindingTypeEXT::SampledTexture2D,
                                     CNA::ShaderStageEXT::Fragment)});
    ShaderEffect effect(device, package);
    ASSERT_TRUE(effect.IsEffectValid());
    effect.SetStorageTextureEXT(0, image);
    image.Dispose();
    RenderTarget2D target(device, 4, 4);
    const std::array<std::array<float, 3>, 6> corners{{
        {-1, -1, 0}, {1, -1, 0}, {1, 1, 0},
        {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    VertexBuffer quad(device,
                      VertexDeclaration(12, {VertexElement(
                          0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)}),
                      6, BufferUsage::None);
    quad.SetDataRaw(corners.data(), 6, 12);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.SetVertexBuffer(&quad);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device.SetVertexBuffer(nullptr);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    std::array<Color, 16> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (const Color& pixel : pixels)
        EXPECT_EQ(pixel.getPackedValueProperty(), Color::Lime.getPackedValueProperty());
    effect.ClearStorageTextureEXT(0);
}

TEST(D3D12OrdinaryImageTest, ComputeWriteReachesClassicSpriteBatch)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    CnaTest::EngineLayer::HiDefDevice device;
    ASSERT_TRUE(device.SupportsRendererFeatureEXT(CNA::RendererFeature::ComputeImageBinding));
    Texture2D image(device, 1, 1);
    ComputeShader writer(device, R"(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = float4(0, 1, 0, 1);
}
)");
    writer.bindImage(0, image, CNA::GraphicsImageAccess::WriteOnly);
    writer.dispatch(1);

    RenderTarget2D target(device, 4, 4);
    SpriteBatch sprites(device);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sprites.Begin();
    sprites.Draw(image, Microsoft::Xna::Framework::Rectangle(0, 0, 4, 4), Color::White);
    sprites.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    std::array<Color, 16> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (const Color& pixel : pixels)
        EXPECT_EQ(pixel.getPackedValueProperty(), Color::Lime.getPackedValueProperty());
}

TEST(D3D12OrdinaryImageTest, SingleTypedUavSurvivesExplicitDeviceRecreation)
{
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    CnaTest::EngineLayer::HiDefDevice device;
    auto& renderer = dynamic_cast<DirectX12Renderer&>(device.GetRenderer());
    constexpr auto access = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite));
    if (!device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Single).Supports(access))
        GTEST_SKIP() << "the physical adapter has no typed R32_FLOAT UAV load/store";
    Texture2D image(device, 1, 1, false, SurfaceFormat::Single);
    auto& native = dynamic_cast<CNA::Internal::Renderers::DirectX12::D3D12TextureRenderer&>(
        image.GetRenderer());
    ASSERT_NE(native.GetUnorderedAccessViewGpuHandleEXT().ptr, 0u);
    renderer.RecreateDeviceEXT();
    ASSERT_NE(native.GetUnorderedAccessViewGpuHandleEXT().ptr, 0u);
    EXPECT_EQ(native.GetImageAccessEXT() & static_cast<std::uint32_t>(access),
              static_cast<std::uint32_t>(access));
    ComputeShader writer(device, R"(
RWTexture2D<float> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Output[id.xy] = 2.75f; }
)");
    writer.bindImage(0, image, CNA::GraphicsImageAccess::WriteOnly);
    writer.dispatch(1);
    float actual = 0.0f;
    ASSERT_TRUE(native.GetData(0, 0, 0, 1, 1, &actual, sizeof(actual)));
    EXPECT_EQ(actual, 2.75f);
}

TEST(D3D12OrdinaryImageTest, WritableAliasesAndSampledAliasingAreRejected)
{
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    CnaTest::EngineLayer::HiDefDevice device;
    Texture2D image(device, 1, 1);
    ComputeShader writer(device, R"(
Texture2D<float4> Source : register(t0);
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = Source.Load(uint3(id.xy, 0));
}
)");
    writer.bindTexture(0, "Source", image);
    writer.bindImage(0, image, CNA::GraphicsImageAccess::ReadWrite);
    EXPECT_THROW(writer.dispatch(1), std::invalid_argument);

    ComputeShader duplicate(device, R"(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Output[id.xy] = 1.0; }
)");
    duplicate.bindImage(0, image, CNA::GraphicsImageAccess::WriteOnly);
    duplicate.bindImage(1, image, CNA::GraphicsImageAccess::WriteOnly);
    EXPECT_THROW(duplicate.dispatch(1), std::invalid_argument);
}

TEST(D3D12OrdinaryImageTest, BindingKeepsDisposedPublicTextureAliveThroughDispatch)
{
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    CnaTest::EngineLayer::HiDefDevice device;
    auto image = std::make_unique<Texture2D>(device, 1, 1);
    auto* native = dynamic_cast<CNA::Internal::Renderers::DirectX12::D3D12TextureRenderer*>(
        &image->GetRenderer());
    ASSERT_NE(native, nullptr);
    ComputeShader writer(device, R"(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Output[id.xy] = float4(1, 0, 0, 1); }
)");
    writer.bindImage(0, *image, CNA::GraphicsImageAccess::WriteOnly);
    image.reset();
    writer.dispatch(1);
    std::array<std::uint8_t, 4> actual{};
    ASSERT_TRUE(native->GetData(0, 0, 0, 1, 1, actual.data(), actual.size()));
    EXPECT_EQ(actual, (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
}

#endif // CNA_RENDERER_DIRECTX12 && CNA_CNAEXT
