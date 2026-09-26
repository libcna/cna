// SPDX-License-Identifier: MS-PL
#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Graphics/StorageTexture2D.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"
#include "EngineTestSupport.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#ifdef CNA_RENDERER_DIRECTX11
#include "CNA/Internal/Renderers/DirectX11/D3D11ComputeShader.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11IndirectBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#endif

TEST(D3D11NativeComputeTest, AComputeWriteRoundTripsThroughGpuStorage)
{
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native HLSL compute probe targets DirectX 11";
    ASSERT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders));
    ASSERT_TRUE(device.SupportsShaderLanguageEXT(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute));

    constexpr std::size_t kCount = 128;
    std::array<std::uint32_t, kCount> input{};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<std::uint32_t>(i + 1);

    CNA::Graphics::StorageBuffer buffer(device, sizeof(input));
    buffer.setBytes(input.data(), sizeof(input));

    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11NativeComputeTest.hlsl", R"(
RWByteAddressBuffer Values : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint byteOffset = id.x * 4;
    Values.Store(byteOffset, Values.Load(byteOffset) * 2);
}

)");
    CNA::Graphics::ComputeShader shader(device, source);
    shader.bindStorageBuffer(0, buffer);
    shader.dispatch(2);

    std::array<std::uint32_t, kCount> actual{};
    buffer.getBytes(actual.data(), sizeof(actual));
    for (std::size_t i = 0; i < actual.size(); ++i)
        EXPECT_EQ(actual[i], input[i] * 2) << "element " << i;
}

TEST(D3D11NativeComputeTest, ComputeWriteReachesVertexStorageRead)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this raw vertex-storage probe targets DirectX 11";
    ASSERT_GE(device.GetRenderer().GetMaxVertexShaderStorageBlocksEXT(), 7);

    CNA::Graphics::StorageBuffer buffer(device, 16);
    const float input = 0.25f;
    buffer.setBytes(&input, sizeof(input));
    const CNA::Graphics::ShaderCodeEXT computeSource(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11VertexStorageWrite.hlsl", R"HLSL(
RWByteAddressBuffer Values : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Values.Store(0, asuint(asfloat(Values.Load(0)) * 2.0f));
}
)HLSL");
    CNA::Graphics::ComputeShader compute(device, computeSource);
    compute.bindStorageBuffer(0, buffer);
    compute.dispatch(1);

    ShaderEffect effect(device, R"HLSL(
ByteAddressBuffer Values : register(t6);
struct Input { float3 position : POSITION; float4 color : COLOR; };
struct Output { float4 position : SV_Position; float4 color : COLOR0; };
Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 1.0f);
    output.color = float4(asfloat(Values.Load(0)), 0.0f, 0.0f, 1.0f);
    return output;
}
)HLSL", R"HLSL(
float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target0
{
    return color;
}
)HLSL");
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    constexpr int size = 64;
    RenderTarget2D target(device, size, size);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    const std::array<VertexPositionColor, 3> triangle{
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(-1.0f, 3.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(3.0f, -1.0f, 0.0f), Color::White),
    };
    effect.Apply();
    EXPECT_THROW(
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, triangle.data(), 0, 1),
        System::NotSupportedException);
    device.GetRenderer().BindStorageBufferForDrawEXT(6, *buffer.getRendererEXT());
    device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    device.SetRenderTarget(nullptr);

    std::array<Color, size * size> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color centre = pixels[(size / 2) * size + size / 2];
    EXPECT_NEAR(centre.getRProperty(), 128, 3);
    EXPECT_EQ(centre.getGProperty(), 0);
    EXPECT_EQ(centre.getBProperty(), 0);

    ShaderEffect pixelEffect(device, R"HLSL(
struct Input { float3 position : POSITION; float4 color : COLOR; };
struct Output { float4 position : SV_Position; float4 color : COLOR0; };
Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}
)HLSL", R"HLSL(
ByteAddressBuffer Values : register(t6);
float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target0
{
    return float4(0.0f, asfloat(Values.Load(0)), 0.0f, 1.0f);
}
)HLSL");
    ASSERT_TRUE(pixelEffect.IsEffectValid()) << pixelEffect.GetCompileErrorEXT();
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    pixelEffect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    device.SetRenderTarget(nullptr);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color pixelStageCentre = pixels[(size / 2) * size + size / 2];
    EXPECT_EQ(pixelStageCentre.getRProperty(), 0);
    EXPECT_NEAR(pixelStageCentre.getGProperty(), 128, 3);
    EXPECT_EQ(pixelStageCentre.getBProperty(), 0);

    Microsoft::Xna::Framework::Graphics::Texture2D sampled(device, 1, 1);
    const Color blue = Color::Blue;
    sampled.SetData(&blue, 1);
    ShaderEffect textureEffect(device, R"HLSL(
Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);
struct Input { float3 position : POSITION; float4 color : COLOR; };
struct Output { float4 position : SV_Position; float4 color : COLOR0; };
Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 1.0f);
    output.color = Source.SampleLevel(SourceSampler, float2(0.5f, 0.5f), 0.0f);
    return output;
}
)HLSL", R"HLSL(
float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target0
{
    return color;
}
)HLSL");
    ASSERT_TRUE(textureEffect.IsEffectValid()) << textureEffect.GetCompileErrorEXT();
    textureEffect.SetTexture(0, sampled);
    device.getSamplerStatesProperty()[0] =
        Microsoft::Xna::Framework::Graphics::SamplerState::PointClamp;
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    textureEffect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    device.SetRenderTarget(nullptr);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color textureStageCentre = pixels[(size / 2) * size + size / 2];
    EXPECT_EQ(textureStageCentre.getRProperty(), 0);
    EXPECT_EQ(textureStageCentre.getGProperty(), 0);
    EXPECT_EQ(textureStageCentre.getBProperty(), 255);

#ifdef CNA_RENDERER_DIRECTX11
    auto* renderer = dynamic_cast<
        CNA::Internal::Renderers::DirectX11::DirectX11Renderer*>(
            &device.GetRenderer());
    auto* native = dynamic_cast<
        CNA::Internal::Renderers::DirectX11::D3D11IndirectBuffer*>(
            buffer.getRendererEXT());
    ASSERT_NE(renderer, nullptr);
    ASSERT_NE(native, nullptr);
    ID3D11ShaderResourceView* expected = native->GetShaderResourceViewEXT();
    renderer->GetContextEXT()->VSSetShaderResources(6, 1, &expected);
    compute.dispatch(1);
    ID3D11ShaderResourceView* restored = nullptr;
    renderer->GetContextEXT()->VSGetShaderResources(6, 1, &restored);
    EXPECT_EQ(restored, expected);
    if (restored) restored->Release();
    ID3D11ShaderResourceView* empty = nullptr;
    renderer->GetContextEXT()->VSSetShaderResources(6, 1, &empty);
    float doubledAgain = 0.0f;
    buffer.getBytes(&doubledAgain, sizeof(doubledAgain));
    EXPECT_FLOAT_EQ(doubledAgain, 1.0f);
#endif
}
TEST(D3D11NativeComputeTest, StorageImageTransfersPreserveMipsAndRectangles)
{
    using CNA::Graphics::StorageTexture2D;
    using CNA::Graphics::StorageTexture2DDescriptor;
    using CNA::Graphics::StorageTexture2DUsage;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native storage-image probe targets DirectX 11";
    constexpr std::uint32_t required =
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped);
    if (!device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Color).Supports(
            static_cast<CNA::RendererFormatUsage>(required)))
        GTEST_SKIP() << "this D3D11 adapter does not support typed Color UAV stores";

    constexpr auto usage = StorageTexture2DUsage::StorageWrite |
        StorageTexture2DUsage::TransferSource |
        StorageTexture2DUsage::TransferDestination;
    StorageTexture2D image(device, StorageTexture2DDescriptor(
        7, 5, 3, SurfaceFormat::Color, usage));
    std::array<std::uint8_t, 7 * 5 * 4> base{};
    std::array<std::uint8_t, 3 * 2 * 4> middle{};
    std::array<std::uint8_t, 4> tip{211, 73, 29, 255};
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
    EXPECT_EQ(base, actualBase);
    EXPECT_EQ(middle, actualMiddle);
    EXPECT_EQ(tip, actualTip);
    EXPECT_EQ(patch, actualPatch);
}

TEST(D3D11NativeComputeTest, TypedStorageImageReadReachesAnotherGpuResource)
{
    using CNA::Graphics::StorageTexture2D;
    using CNA::Graphics::StorageTexture2DDescriptor;
    using CNA::Graphics::StorageTexture2DUsage;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native typed-image probe targets DirectX 11";
    const auto support = device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Color);
    constexpr auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination));
    if (!support.Supports(required))
        GTEST_SKIP() << "this D3D11 adapter lacks typed Color UAV loads";

    StorageTexture2D image(device, StorageTexture2DDescriptor(
        1, 1, 1, SurfaceFormat::Color,
        StorageTexture2DUsage::StorageRead |
            StorageTexture2DUsage::TransferDestination));
    const std::array<std::uint8_t, 4> pixel{64, 128, 192, 255};
    image.setData(0, nullptr, pixel.data(), pixel.size());
    CNA::Graphics::StorageBuffer output(device, 16);

    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11TypedStorageImageRead.hlsl", R"(
RWTexture2D<unorm float4> Source : register(u0);
RWByteAddressBuffer Output : register(u1);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output.Store4(0, asuint(Source[uint2(0, 0)]));
}
)");
    CNA::Graphics::ComputeShader shader(device, source);
    shader.bindStorageTexture(0, image, CNA::GraphicsImageAccess::ReadOnly);
    shader.bindStorageBuffer(1, output);
    shader.dispatch(1);
    std::array<float, 4> actual{};
    output.getBytes(actual.data(), sizeof(actual));
    for (std::size_t channel = 0; channel < actual.size(); ++channel)
        EXPECT_NEAR(actual[channel], static_cast<float>(pixel[channel]) / 255.0f,
                    1.0f / 512.0f) << "channel " << channel;
}

TEST(D3D11NativeComputeTest, StorageImagePackageUsesItsOwnLimit)
{
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native package-selection probe targets DirectX 11";
    const auto limit = device.GetRendererLimitEXT(
        CNA::RendererLimit::MaxStorageImagesPerShaderStage);
    if (!limit.known || limit.value == 0)
        GTEST_SKIP() << "this D3D11 adapter has no typed Color storage image";
    EXPECT_FALSE(device.SupportsRendererFeatureEXT(
        CNA::RendererFeature::ComputeImageBinding));
    const CNA::Graphics::ShaderPackageEXT package(
        {CNA::Graphics::ShaderCodeEXT(
            CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
            "main", "D3D11StorageImagePackage.hlsl", R"(
RWTexture2D<unorm float4> Image : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Image[uint2(0, 0)] = float4(1, 0, 0, 1);
}
)" )},
        {CNA::ShaderStageEXT::Compute},
        {CNA::Graphics::ShaderBindingRequirementEXT(
            "Image", 0, CNA::Graphics::ShaderBindingTypeEXT::StorageTexture2D,
            CNA::ShaderStageEXT::Compute)});
    const auto selection = package.selectFor(device);
    EXPECT_TRUE(selection.isUsable()) << selection.getDiagnostic();
}

#ifdef CNA_RENDERER_DIRECTX11
TEST(D3D11NativeComputeTest, ComputeImageWriteRestoresAliasingPixelShaderResource)
{
    using CNA::Internal::Renderers::DirectX11::D3D11ComputeShader;
    using CNA::Internal::Renderers::DirectX11::D3D11StorageTexture2D;
    using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    CnaTest::EngineLayer::HiDefDevice device;
    auto* renderer = dynamic_cast<DirectX11Renderer*>(&device.GetRenderer());
    ASSERT_NE(renderer, nullptr);
    if (device.GetRendererLimitEXT(CNA::RendererLimit::MaxStorageImagesPerShaderStage).value == 0)
        GTEST_SKIP() << "this D3D11 adapter has no typed Color UAV store";

    constexpr std::uint32_t usage = UINT32_C(0x02) | UINT32_C(0x04) | UINT32_C(0x10);
    auto image = std::make_shared<D3D11StorageTexture2D>(
        renderer->GetDeviceEXT(), renderer->GetContextEXT(), 1, 1, 1, usage);
    D3D11ComputeShader compute(renderer->GetDeviceEXT(), renderer->GetContextEXT());
    ASSERT_TRUE(compute.CompileProgram(R"(
RWTexture2D<unorm float4> Target : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Target[uint2(0, 0)] = float4(1, 0, 0, 1);
}
)")) << compute.GetCompileError();
    ASSERT_TRUE(compute.BindStorageTexture2DEXT(0, image, 1));

    ID3D11ShaderResourceView* expected = image->GetShaderResourceViewEXT();
    renderer->GetContextEXT()->PSSetShaderResources(0, 1, &expected);
    compute.Dispatch(1, 1, 1);
    ID3D11ShaderResourceView* restored = nullptr;
    renderer->GetContextEXT()->PSGetShaderResources(0, 1, &restored);
    EXPECT_EQ(restored, expected);
    if (restored) restored->Release();
    ID3D11ShaderResourceView* empty = nullptr;
    renderer->GetContextEXT()->PSSetShaderResources(0, 1, &empty);

    std::array<std::uint8_t, 4> pixel{};
    ASSERT_TRUE(image->GetData(0, 0, 0, 1, 1, pixel.data(), pixel.size()));
    EXPECT_EQ(pixel, (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
}

TEST(D3D11NativeComputeTest, SampledStorageImageBindingRetainsNativeResource)
{
    using CNA::Internal::Renderers::DirectX11::D3D11EffectRenderer;
    using CNA::Internal::Renderers::DirectX11::D3D11StorageTexture2D;
    using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    CnaTest::EngineLayer::HiDefDevice device;
    auto* renderer = dynamic_cast<DirectX11Renderer*>(&device.GetRenderer());
    ASSERT_NE(renderer, nullptr);
    if (device.GetRendererLimitEXT(CNA::RendererLimit::MaxStorageImagesPerShaderStage).value == 0)
        GTEST_SKIP() << "this D3D11 adapter has no typed Color UAV store";

    D3D11EffectRenderer effect(renderer->GetDeviceEXT(), renderer->GetContextEXT());
    ASSERT_TRUE(effect.CompileProgram(R"(
struct Input { float2 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
float4 main(Input input) : SV_POSITION
{
    return float4(input.position + input.uv * 0.000001 +
                  input.color.xy * 0.000001, 0, 1);
}
)", R"(
Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);
float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    return Source.SampleLevel(SourceSampler, float2(0.5, 0.5), 0);
}
)")) << effect.GetCompileError();

    constexpr std::uint32_t usage = UINT32_C(0x02) | UINT32_C(0x04);
    auto image = std::make_shared<D3D11StorageTexture2D>(
        renderer->GetDeviceEXT(), renderer->GetContextEXT(), 1, 1, 1, usage);
    ID3D11ShaderResourceView* expected = image->GetShaderResourceViewEXT();
    ASSERT_TRUE(effect.BindStorageTexture2DEXT(0, image));
    image.reset();
    effect.Bind();
    ID3D11ShaderResourceView* actual = nullptr;
    renderer->GetContextEXT()->PSGetShaderResources(0, 1, &actual);
    EXPECT_EQ(actual, expected);
    if (actual) actual->Release();

    ASSERT_TRUE(effect.BindStorageTexture2DEXT(0, nullptr));
    effect.Bind();
    actual = nullptr;
    renderer->GetContextEXT()->PSGetShaderResources(0, 1, &actual);
    EXPECT_EQ(actual, nullptr);
    if (actual) actual->Release();
}
#endif

#endif
