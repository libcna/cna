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
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#ifdef CNA_RENDERER_DIRECTX11
#include "CNA/Internal/Renderers/DirectX11/D3D11ComputeShader.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11IndirectBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#endif

TEST(D3D11NativeComputeTest, VertexAndColorLimitsMatchTheNativeDrawPaths)
{
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native limit probe targets DirectX 11";

    const auto bindings = device.GetRendererLimitEXT(
        CNA::RendererLimit::MaxVertexInputBindings);
    const auto attributes = device.GetRendererLimitEXT(
        CNA::RendererLimit::MaxVertexInputAttributes);
    const auto colors = device.GetRendererLimitEXT(
        CNA::RendererLimit::MaxColorAttachments);
    ASSERT_TRUE(bindings.known);
    ASSERT_TRUE(attributes.known);
    ASSERT_TRUE(colors.known);
    EXPECT_EQ(bindings.value, 16u);
    EXPECT_EQ(attributes.value, 32u);
    EXPECT_EQ(colors.value, 4u);
}

#ifdef CNA_RENDERER_DIRECTX11
TEST(D3D11NativeComputeTest, DebugMarkersFollowTheImmediateContextAcrossRecovery)
{
    using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    CnaTest::EngineLayer::HiDefDevice device;
    auto* renderer = dynamic_cast<DirectX11Renderer*>(&device.GetRenderer());
    ASSERT_NE(renderer, nullptr);

    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> original;
    ASSERT_TRUE(SUCCEEDED(renderer->GetContextEXT()->QueryInterface(
        IID_PPV_ARGS(original.GetAddressOf()))));
    device.SetStringMarkerEXT("CNA D3D11 marker \xE2\x9C\x93");
    device.SetStringMarkerEXT("");
    renderer->SetStringMarkerEXT(nullptr);
    original.Reset();

    renderer->DebugSimulateContextLoss();
    renderer->DebugRestoreContext();
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> restored;
    ASSERT_TRUE(SUCCEEDED(renderer->GetContextEXT()->QueryInterface(
        IID_PPV_ARGS(restored.GetAddressOf()))));
    device.SetStringMarkerEXT("CNA D3D11 recovered marker");
}
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

TEST(D3D11NativeComputeTest, ComputeImageWriteReachesTheNextGraphicsDraw)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native image and graphics probe targets DirectX 11";
    if (!device.SupportsRendererFeatureEXT(CNA::RendererFeature::ComputeImageBinding))
        GTEST_SKIP() << "this D3D11 adapter has no typed Color image read and write";

    Texture2D image(device, 1, 1);
    const Color black = Color::Black;
    image.SetData(&black, 1);
    const CNA::Graphics::ShaderCodeEXT computeSource(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11ColorImageWrite.hlsl", R"HLSL(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = float4(0.25f, 0.5f, 0.75f, 1.0f);
}
)HLSL");
    CNA::Graphics::ComputeShader compute(device, computeSource);
    compute.bindImage(0, image, CNA::GraphicsImageAccess::WriteOnly);
    compute.dispatch(1);

    ShaderEffect effect(device, R"HLSL(
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
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
    effect.SetTexture(0, image);
    device.getSamplerStatesProperty()[0] =
        Microsoft::Xna::Framework::Graphics::SamplerState::PointClamp;
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
    device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    device.SetRenderTarget(nullptr);
    std::array<Color, size * size> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color centre = pixels[(size / 2) * size + size / 2];
    EXPECT_NEAR(centre.getRProperty(), 64, 2);
    EXPECT_NEAR(centre.getGProperty(), 128, 2);
    EXPECT_NEAR(centre.getBProperty(), 191, 2);
}

TEST(D3D11NativeComputeTest, SameTextureCannotBeSampledAndBoundAsAnImage)
{
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this native image hazard probe targets DirectX 11";
    if (!device.SupportsRendererFeatureEXT(CNA::RendererFeature::ComputeImageBinding))
        GTEST_SKIP() << "this D3D11 adapter has no typed Color image read and write";
    Microsoft::Xna::Framework::Graphics::Texture2D image(device, 1, 1);
    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11ImageAlias.hlsl", R"HLSL(
Texture2D<float4> Source : register(t0);
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = Source.Load(uint3(id.xy, 0));
}
)HLSL");
    CNA::Graphics::ComputeShader compute(device, source);
    compute.bindTexture(0, "Source", image);
    compute.bindImage(0, image, CNA::GraphicsImageAccess::ReadWrite);
    EXPECT_THROW(compute.dispatch(1), std::invalid_argument);
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

TEST(D3D11NativeComputeTest, TypedFloatStorageImagesPreserveExactTransferBytes)
{
    using CNA::Graphics::StorageTexture2D;
    using CNA::Graphics::StorageTexture2DDescriptor;
    using CNA::Graphics::StorageTexture2DUsage;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this typed storage-image probe targets DirectX 11";

    constexpr auto usage = StorageTexture2DUsage::StorageWrite |
        StorageTexture2DUsage::TransferSource |
        StorageTexture2DUsage::TransferDestination;
    constexpr auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination));
    struct FormatCase { SurfaceFormat format; int bytesPerTexel; };
    constexpr std::array formats{
        FormatCase{SurfaceFormat::Single, 4},
        FormatCase{SurfaceFormat::Vector2, 8},
        FormatCase{SurfaceFormat::Vector4, 16},
        FormatCase{SurfaceFormat::HalfSingle, 2},
        FormatCase{SurfaceFormat::HalfVector2, 4},
        FormatCase{SurfaceFormat::HalfVector4, 8},
    };
    int supported = 0;
    for (const auto [format, bytesPerTexel] : formats)
    {
        if (!device.GetRendererSurfaceFormatSupportEXT(format).Supports(required))
            continue;
        ++supported;
        SCOPED_TRACE(static_cast<int>(format));
        StorageTexture2D image(device, StorageTexture2DDescriptor(
            4, 3, 1, format, usage));
        std::printf("[INFO] D3D11 typed storage format ordinal %d, bytes/texel %d\n",
                    static_cast<int>(format), bytesPerTexel);
        std::vector<std::uint8_t> expected(
            static_cast<std::size_t>(4 * 3 * bytesPerTexel));
        for (std::size_t index = 0; index < expected.size(); ++index)
            expected[index] = static_cast<std::uint8_t>(index * 37u + 11u);
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
    }
    std::printf("[INFO] D3D11 typed float UAV formats with exact transfers: %d/%zu\n",
                supported, formats.size());
    EXPECT_GT(supported, 0) << "the physical D3D11 adapter reported no typed float UAV store";
}

TEST(D3D11NativeComputeTest, SingleStorageImageComputeWriteAndReadback)
{
    using CNA::Graphics::StorageTexture2D;
    using CNA::Graphics::StorageTexture2DDescriptor;
    using CNA::Graphics::StorageTexture2DUsage;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    CnaTest::EngineLayer::HiDefDevice device;
    if (device.GetGraphicsRendererName() != "DIRECTX11")
        GTEST_SKIP() << "this typed storage-image probe targets DirectX 11";
    constexpr auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination));
    if (!device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Single).Supports(required))
        GTEST_SKIP() << "this D3D11 adapter lacks typed R32_FLOAT UAV read/write";

    StorageTexture2D image(device, StorageTexture2DDescriptor(
        2, 2, 1, SurfaceFormat::Single,
        StorageTexture2DUsage::StorageRead |
            StorageTexture2DUsage::StorageWrite |
            StorageTexture2DUsage::TransferSource |
            StorageTexture2DUsage::TransferDestination));
    const std::array<float, 4> initial{0.5f, 1.25f, 2.0f, 3.5f};
    image.setData(0, nullptr, initial.data(), sizeof(initial));
    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11SingleStorageImage.hlsl", R"HLSL(
RWTexture2D<float> Image : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Image[uint2(1, 1)] = Image[uint2(1, 1)] * 2.0f + 0.25f;
}
)HLSL");
    CNA::Graphics::ComputeShader shader(device, source);
    shader.bindStorageTexture(0, image, CNA::GraphicsImageAccess::ReadWrite);
    shader.dispatch(1);
    std::array<float, 4> actual{};
    image.getData(0, nullptr, actual.data(), sizeof(actual));
    EXPECT_EQ(actual[0], initial[0]);
    EXPECT_EQ(actual[1], initial[1]);
    EXPECT_EQ(actual[2], initial[2]);
    EXPECT_EQ(actual[3], 7.25f);
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
    const auto colorSupport = device.GetRendererSurfaceFormatSupportEXT(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
    constexpr auto imageAccess = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite));
    EXPECT_EQ(device.SupportsRendererFeatureEXT(
                  CNA::RendererFeature::ComputeImageBinding),
              colorSupport.Supports(imageAccess));
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
TEST(D3D11NativeComputeTest, OrdinarySingleTextureIsWritableAsTypedImage)
{
    using CNA::Internal::Renderers::DirectX11::D3D11TextureRenderer;
    using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    CnaTest::EngineLayer::HiDefDevice device;
    auto* renderer = dynamic_cast<DirectX11Renderer*>(&device.GetRenderer());
    ASSERT_NE(renderer, nullptr);
    constexpr auto access = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite));
    if (!device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Single).Supports(access))
        GTEST_SKIP() << "this D3D11 adapter lacks typed R32_FLOAT UAV read/write";

    Texture2D image(device, 1, 1, false, SurfaceFormat::Single);
    auto* native = dynamic_cast<D3D11TextureRenderer*>(&image.GetRenderer());
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native->GetUnorderedAccessViewEXT(), nullptr);
    renderer->DebugSimulateContextLoss();
    renderer->DebugRestoreContext();
    ASSERT_NE(native->GetUnorderedAccessViewEXT(), nullptr);
    EXPECT_EQ(native->GetDeviceEXT(), renderer->GetDeviceEXT());
    EXPECT_EQ(native->GetImageAccessEXT() & static_cast<std::uint32_t>(access),
              static_cast<std::uint32_t>(access));
    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11SingleTextureImage.hlsl", R"HLSL(
RWTexture2D<float> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = 2.75f;
}
)HLSL");
    CNA::Graphics::ComputeShader shader(device, source);
    shader.bindImage(0, image, CNA::GraphicsImageAccess::WriteOnly);
    shader.dispatch(1);

    D3D11_TEXTURE2D_DESC description{};
    native->GetTextureEXT()->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->CreateTexture2D(
        &description, nullptr, staging.GetAddressOf())));
    renderer->GetContextEXT()->CopyResource(staging.Get(), native->GetTextureEXT());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    ASSERT_TRUE(SUCCEEDED(renderer->GetContextEXT()->Map(
        staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    float actual = 0.0f;
    std::memcpy(&actual, mapped.pData, sizeof(actual));
    renderer->GetContextEXT()->Unmap(staging.Get(), 0);
    EXPECT_EQ(actual, 2.75f);
}

TEST(D3D11NativeComputeTest, ImageBindingRetainsTextureAfterPublicDestruction)
{
    using CNA::Internal::Renderers::DirectX11::D3D11TextureRenderer;
    using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    CnaTest::EngineLayer::HiDefDevice device;
    auto* renderer = dynamic_cast<DirectX11Renderer*>(&device.GetRenderer());
    ASSERT_NE(renderer, nullptr);
    if (!renderer->SupportsComputeImageBindingEXT())
        GTEST_SKIP() << "this D3D11 adapter has no typed Color image read and write";

    auto image = std::make_unique<Texture2D>(device, 1, 1);
    auto* native = dynamic_cast<D3D11TextureRenderer*>(&image->GetRenderer());
    ASSERT_NE(native, nullptr);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> resource = native->GetTextureEXT();
    ASSERT_NE(resource, nullptr);
    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11RetainedImage.hlsl", R"HLSL(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = float4(1.0f, 0.0f, 0.0f, 1.0f);
}
)HLSL");
    CNA::Graphics::ComputeShader compute(device, source);
    compute.bindImage(0, *image, CNA::GraphicsImageAccess::WriteOnly);
    image.reset();
    compute.dispatch(1);

    D3D11_TEXTURE2D_DESC description{};
    resource->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->CreateTexture2D(
        &description, nullptr, staging.GetAddressOf())));
    renderer->GetContextEXT()->CopyResource(staging.Get(), resource.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    ASSERT_TRUE(SUCCEEDED(renderer->GetContextEXT()->Map(
        staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData);
    EXPECT_EQ(pixel[0], 255);
    EXPECT_EQ(pixel[1], 0);
    EXPECT_EQ(pixel[2], 0);
    EXPECT_EQ(pixel[3], 255);
    renderer->GetContextEXT()->Unmap(staging.Get(), 0);
}

TEST(D3D11NativeComputeTest, ImageViewWorksAfterDeviceRecovery)
{
    using CNA::Internal::Renderers::DirectX11::D3D11TextureRenderer;
    using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    CnaTest::EngineLayer::HiDefDevice device;
    auto* renderer = dynamic_cast<DirectX11Renderer*>(&device.GetRenderer());
    ASSERT_NE(renderer, nullptr);
    if (!renderer->SupportsComputeImageBindingEXT())
        GTEST_SKIP() << "this D3D11 adapter has no typed Color image read and write";
    Microsoft::Xna::Framework::Graphics::Texture2D image(device, 1, 1);
    auto* native = dynamic_cast<D3D11TextureRenderer*>(&image.GetRenderer());
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native->GetUnorderedAccessViewEXT(), nullptr);

    renderer->DebugSimulateContextLoss();
    renderer->DebugRestoreContext();
    ASSERT_NE(native->GetUnorderedAccessViewEXT(), nullptr);
    EXPECT_EQ(native->GetDeviceEXT(), renderer->GetDeviceEXT());

    const CNA::Graphics::ShaderCodeEXT source(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Compute,
        "main", "D3D11RecoveredImage.hlsl", R"HLSL(
RWTexture2D<unorm float4> Output : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Output[id.xy] = float4(0.0f, 1.0f, 0.0f, 1.0f);
}
)HLSL");
    CNA::Graphics::ComputeShader compute(device, source);
    compute.bindImage(0, image, CNA::GraphicsImageAccess::WriteOnly);
    compute.dispatch(1);
    std::array<std::uint8_t, 4> pixel{};
    ASSERT_TRUE(native->GetData(0, 0, 0, 1, 1, pixel.data(),
                                static_cast<int>(pixel.size())));
    EXPECT_EQ(pixel[0], 0);
    EXPECT_EQ(pixel[1], 255);
    EXPECT_EQ(pixel[2], 0);
    EXPECT_EQ(pixel[3], 255);
}

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
        renderer->GetDeviceEXT(), renderer->GetContextEXT(), 1, 1, 1,
        static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color), usage);
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
        renderer->GetDeviceEXT(), renderer->GetContextEXT(), 1, 1, 1,
        static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color), usage);
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
