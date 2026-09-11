// SPDX-License-Identifier: MS-PL

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/Texture2DArray.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using CNA::Graphics::Texture2DArray;
using CNA::Graphics::Texture2DArrayDescriptor;
using CNA::Graphics::Texture2DArrayUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace CNA::Internal
{
    class Texture2DArrayGraphicsDeviceTestPeer
    {
    public:
        static void ReplaceRenderer(
            GraphicsDevice& device,
            std::unique_ptr<CNA::Internal::Renderers::IGraphicsRenderer> renderer)
        {
            device.renderer_ = std::move(renderer);
            device.InvalidateRendererCapabilityProfileEXT();
        }

        static void InvalidateCapabilityProfile(GraphicsDevice& device)
        {
            device.InvalidateRendererCapabilityProfileEXT();
        }
    };
}

namespace
{
    struct TransferCall
    {
        int layer = -1;
        int mipLevel = -1;
        int x = -1;
        int y = -1;
        int width = -1;
        int height = -1;
        std::vector<std::uint8_t> bytes;
    };

    struct TextureArrayTestState
    {
        bool acceptUpload = true;
        bool acceptReadback = true;
        bool acceptBinding = true;
        int uploadCalls = 0;
        int readbackCalls = 0;
        int bindingCalls = 0;
        int lastBindingUnit = -1;
        TransferCall upload;
        TransferCall readback;
        std::vector<std::uint8_t> readbackBytes;
        std::shared_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer> boundArray;
    };

    class RecordingTexture2DArrayRenderer final
        : public CNA::Internal::Renderers::ITexture2DArrayRenderer
    {
    public:
        RecordingTexture2DArrayRenderer(
            int& destructionCount, const std::shared_ptr<TextureArrayTestState>& state)
            : destructionCount_(destructionCount)
            , state_(state)
        {
        }

        ~RecordingTexture2DArrayRenderer() override { ++destructionCount_; }

        [[nodiscard]] bool SetData(
            int layer, int mipLevel, int x, int y, int width, int height,
            const void* data, std::size_t byteCount) override
        {
            ++state_->uploadCalls;
            state_->upload = {layer, mipLevel, x, y, width, height, {}};
            const auto* begin = static_cast<const std::uint8_t*>(data);
            state_->upload.bytes.assign(begin, begin + byteCount);
            return state_->acceptUpload;
        }

        [[nodiscard]] bool GetData(
            int layer, int mipLevel, int x, int y, int width, int height,
            void* data, std::size_t byteCount) const override
        {
            ++state_->readbackCalls;
            state_->readback = {layer, mipLevel, x, y, width, height, {}};
            if (!state_->acceptReadback || state_->readbackBytes.size() != byteCount)
                return false;
            std::memcpy(data, state_->readbackBytes.data(), byteCount);
            state_->readback.bytes = state_->readbackBytes;
            return true;
        }

    private:
        int& destructionCount_;
        std::shared_ptr<TextureArrayTestState> state_;
    };

    class RecordingArrayEffectRenderer final
        : public CNA::Internal::Renderers::IEffectRenderer
    {
    public:
        explicit RecordingArrayEffectRenderer(std::shared_ptr<TextureArrayTestState> state)
            : state_(std::move(state))
        {
        }

        bool CompileProgram(const std::string&, const std::string&) override { return true; }
        void Bind() override {}
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return true; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }
        [[nodiscard]] bool BindTexture2DArrayEXT(
            int unit,
            std::shared_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer> texture) override
        {
            ++state_->bindingCalls;
            state_->lastBindingUnit = unit;
            if (!state_->acceptBinding) return false;
            state_->boundArray = std::move(texture);
            return true;
        }

    private:
        std::shared_ptr<TextureArrayTestState> state_;
    };

    class TextureArrayContractRenderer final
        : public CNA::Internal::Renderers::IGraphicsRenderer
    {
    public:
        explicit TextureArrayContractRenderer(int& nativeDestructions)
            : nativeDestructions_(&nativeDestructions)
            , state(std::make_shared<TextureArrayTestState>())
        {
        }

        void Clear(float, float, float, float) override {}
        void Present() override {}
        void GetViewportSize(int& width, int& height) override { width = height = 16; }
        void SetVirtualResolution(int, int) override {}
        void SetPresentationMode(int) override {}
        std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> CreateTexture(
            const CNA::Internal::Graphics::ImageData&) override
        {
            return nullptr;
        }
        std::unique_ptr<CNA::Internal::Renderers::ISpriteBatchRenderer> CreateSpriteBatch() override
        {
            return nullptr;
        }
        void SetRenderTargets(
            const CNA::Internal::Renderers::RenderTargetBindingDescriptor*, int) override {}
        void ClearColorAndDepth(float, float, float, float, float) override {}
        void ClearDepth(float) override {}
        void ClearStencil(int) override {}
        void ClearDepthAndStencil(float, int) override {}
        void ClearColorAndStencil(float, float, float, float, int) override {}
        void ClearColorDepthAndStencil(float, float, float, float, float, int) override {}
        void SetDepthTestEnabled(bool) override {}
        void SetBlendEnabled(bool) override {}
        void SetDepthWriteEnabled(bool) override {}
        std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer> CreateVertexBuffer(
            int) override
        {
            return nullptr;
        }
        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> CreateIndexBuffer16(
            int) override
        {
            return nullptr;
        }
        void DrawColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}
        void DrawIndexedColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const CNA::Internal::Renderers::IIndexBufferRenderer&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}

        [[nodiscard]] int GetMaxTextureDimension() const override { return maxDimension; }
        [[nodiscard]] int GetMaxTextureArrayLayersEXT() const override { return maxLayers; }
        [[nodiscard]] CNA::RendererFormatSupport GetSurfaceFormatUsageSupportEXT(
            int surfaceFormat) const override
        {
            return (supportAllFormats ||
                    surfaceFormat == static_cast<int>(SurfaceFormat::Color))
                ? colorSupport
                : CNA::RendererFormatSupport{};
        }
        std::unique_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer>
        CreateTexture2DArrayEXT(
            int width, int height, int layerCount, int mipLevelCount,
            int surfaceFormat, std::uint32_t usage) override
        {
            ++factoryCalls;
            createdWidth = width;
            createdHeight = height;
            createdLayers = layerCount;
            createdMips = mipLevelCount;
            createdFormat = surfaceFormat;
            createdUsage = usage;
            if (!createResource)
                return nullptr;
            return std::make_unique<RecordingTexture2DArrayRenderer>(
                *nativeDestructions_, state);
        }

        std::unique_ptr<CNA::Internal::Renderers::IEffectRenderer> CreateEffectRenderer(
            const std::string& vertex, const std::string& fragment) override
        {
            auto effect = std::make_unique<RecordingArrayEffectRenderer>(state);
            effect->CompileProgram(vertex, fragment);
            return effect;
        }

        int maxDimension = 8;
        int maxLayers = 3;
        CNA::RendererFormatSupport colorSupport{};
        bool createResource = true;
        bool supportAllFormats = false;
        int factoryCalls = 0;
        int createdWidth = 0;
        int createdHeight = 0;
        int createdLayers = 0;
        int createdMips = 0;
        int createdFormat = -1;
        std::uint32_t createdUsage = 0;
        std::shared_ptr<TextureArrayTestState> state;

    private:
        int* nativeDestructions_;
    };

    constexpr Texture2DArrayUsage FullUsage =
        Texture2DArrayUsage::Sampled |
        Texture2DArrayUsage::Filterable |
        Texture2DArrayUsage::TransferSource |
        Texture2DArrayUsage::TransferDestination;

    [[nodiscard]] constexpr bool HasUsage(
        const Texture2DArrayUsage value, const Texture2DArrayUsage flag)
    {
        return (value & flag) == flag;
    }

    constexpr std::uint32_t FullRequiredFormatUsages =
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Filterable) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped);
}

static_assert(std::is_base_of_v<
              Microsoft::Xna::Framework::Graphics::GraphicsResource, Texture2DArray>);
static_assert(!std::is_copy_constructible_v<Texture2DArray>);
static_assert(!std::is_copy_assignable_v<Texture2DArray>);
static_assert(!std::is_move_constructible_v<Texture2DArray>);
static_assert(!std::is_move_assignable_v<Texture2DArray>);

TEST(Texture2DArrayDescriptorTest, RetainsEveryImmutableFieldAndComposesUsageFlags)
{
    const Texture2DArrayDescriptor descriptor(
        7, 5, 3, 3, SurfaceFormat::HalfVector4, FullUsage);

    EXPECT_EQ(descriptor.getWidth(), 7);
    EXPECT_EQ(descriptor.getHeight(), 5);
    EXPECT_EQ(descriptor.getLayerCount(), 3);
    EXPECT_EQ(descriptor.getMipLevelCount(), 3);
    EXPECT_EQ(descriptor.getFormat(), SurfaceFormat::HalfVector4);
    EXPECT_EQ(descriptor.getUsage(), FullUsage);
    EXPECT_TRUE(HasUsage(descriptor.getUsage(), Texture2DArrayUsage::Sampled));
    EXPECT_TRUE(HasUsage(descriptor.getUsage(), Texture2DArrayUsage::Filterable));
    EXPECT_TRUE(HasUsage(descriptor.getUsage(), Texture2DArrayUsage::TransferSource));
    EXPECT_TRUE(HasUsage(descriptor.getUsage(), Texture2DArrayUsage::TransferDestination));
    EXPECT_EQ(Texture2DArrayUsage::Sampled & Texture2DArrayUsage::TransferSource,
              Texture2DArrayUsage::None);
}

TEST(Texture2DArrayDescriptorTest, RejectsEveryIntrinsicInvalidShapeBeforeADeviceIsNeeded)
{
    const auto sampled = Texture2DArrayUsage::Sampled;
    EXPECT_THROW(Texture2DArrayDescriptor(0, 4, 2, 1, SurfaceFormat::Color, sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(4, 0, 2, 1, SurfaceFormat::Color, sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(4, 4, 0, 1, SurfaceFormat::Color, sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(4, 4, 2, 0, SurfaceFormat::Color, sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(7, 5, 2, 4, SurfaceFormat::Color, sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(
                     4, 4, 2, 1, static_cast<SurfaceFormat>(-1), sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(
                     4, 4, 2, 1, static_cast<SurfaceFormat>(27), sampled),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(
                     4, 4, 2, 1, SurfaceFormat::Color, Texture2DArrayUsage::None),
                 std::invalid_argument);
    EXPECT_THROW(Texture2DArrayDescriptor(
                     4, 4, 2, 1, SurfaceFormat::Color,
                     static_cast<Texture2DArrayUsage>(UINT32_C(1) << 31)),
                 std::invalid_argument);
}

TEST(Texture2DArrayTest, LiveConstructionRequiresTheWholePublishedDeviceContract)
{
    GraphicsDevice device;
    const Texture2DArrayDescriptor descriptor(
        4, 4, 2, 1, SurfaceFormat::Color, Texture2DArrayUsage::Sampled);
    const std::size_t baseline = device.GetTrackedResourceCount();

    const CNA::RendererLimitValue maxDimension =
        device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureDimension);
    const CNA::RendererLimitValue maxLayers =
        device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureArrayLayers);
    const CNA::RendererFormatSupport format =
        device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Color);
    const auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled));
    const bool advertised = maxDimension.known && maxDimension.value >= 4 &&
                            maxLayers.known && maxLayers.value >= 2 &&
                            format.Supports(required);

    if (advertised)
    {
        const Texture2DArray texture(device, descriptor);
        EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
        EXPECT_EQ(texture.getDescriptor().getLayerCount(), 2);
    }
    else
    {
        EXPECT_THROW(Texture2DArray(device, descriptor), System::NotSupportedException);
        EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
    }
}

TEST(Texture2DArrayTest, ValidatesLiveLimitsAndFormatUsageBeforeRendererCreation)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = std::make_unique<TextureArrayContractRenderer>(nativeDestructions);
    TextureArrayContractRenderer* const rendererView = renderer.get();
    renderer->colorSupport = {
        FullRequiredFormatUsages,
        FullRequiredFormatUsages};
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    const std::size_t baseline = device.GetTrackedResourceCount();

    const Texture2DArrayDescriptor tooWide(
        9, 5, 3, 1, SurfaceFormat::Color, Texture2DArrayUsage::Sampled);
    EXPECT_THROW(Texture2DArray(device, tooWide), System::NotSupportedException);
    EXPECT_EQ(rendererView->factoryCalls, 0);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);

    const Texture2DArrayDescriptor tooManyLayers(
        7, 5, 4, 1, SurfaceFormat::Color, Texture2DArrayUsage::Sampled);
    EXPECT_THROW(Texture2DArray(device, tooManyLayers), System::NotSupportedException);
    EXPECT_EQ(rendererView->factoryCalls, 0);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);

    rendererView->colorSupport.supportedUsages &=
        ~static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped);
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    const Texture2DArrayDescriptor unsupportedMipUsage(
        7, 5, 3, 3, SurfaceFormat::Color, FullUsage);
    EXPECT_THROW(Texture2DArray(device, unsupportedMipUsage), System::NotSupportedException);
    EXPECT_EQ(rendererView->factoryCalls, 0);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);

    rendererView->colorSupport.supportedUsages = FullRequiredFormatUsages;
    rendererView->createResource = false;
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(Texture2DArray(device, unsupportedMipUsage), System::NotSupportedException);
    EXPECT_EQ(rendererView->factoryCalls, 1);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);

    rendererView->createResource = true;
    {
        Texture2DArray texture(device, unsupportedMipUsage);
        EXPECT_EQ(rendererView->factoryCalls, 2);
        EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    }
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
}

TEST(Texture2DArrayTest, TransfersPreserveLayerMipRectangleAndExactBytes)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = std::make_unique<TextureArrayContractRenderer>(nativeDestructions);
    TextureArrayContractRenderer* const rendererView = renderer.get();
    renderer->colorSupport = {FullRequiredFormatUsages, FullRequiredFormatUsages};
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    Texture2DArray texture(
        device, Texture2DArrayDescriptor(7, 5, 3, 3, SurfaceFormat::Color, FullUsage));
    const Microsoft::Xna::Framework::Rectangle region(1, 0, 2, 2);
    const std::array<std::uint8_t, 16> source{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    texture.setData(1, 1, &region, source.data(), source.size());

    ASSERT_EQ(rendererView->state->uploadCalls, 1);
    EXPECT_EQ(rendererView->state->upload.layer, 1);
    EXPECT_EQ(rendererView->state->upload.mipLevel, 1);
    EXPECT_EQ(rendererView->state->upload.x, 1);
    EXPECT_EQ(rendererView->state->upload.y, 0);
    EXPECT_EQ(rendererView->state->upload.width, 2);
    EXPECT_EQ(rendererView->state->upload.height, 2);
    EXPECT_EQ(rendererView->state->upload.bytes,
              std::vector<std::uint8_t>(source.begin(), source.end()));

    rendererView->state->readbackBytes.assign(source.rbegin(), source.rend());
    std::array<std::uint8_t, 16> destination{};
    texture.getData(2, 1, &region, destination.data(), destination.size());
    EXPECT_EQ(rendererView->state->readbackCalls, 1);
    EXPECT_EQ(rendererView->state->readback.layer, 2);
    EXPECT_EQ(rendererView->state->readback.mipLevel, 1);
    EXPECT_EQ(rendererView->state->readback.x, 1);
    EXPECT_EQ(rendererView->state->readback.width, 2);
    EXPECT_EQ(std::vector<std::uint8_t>(destination.begin(), destination.end()),
              rendererView->state->readbackBytes);

    rendererView->state->acceptUpload = false;
    EXPECT_THROW(texture.setData(0, 1, &region, source.data(), source.size()),
                 System::NotSupportedException);
    rendererView->state->acceptReadback = false;
    EXPECT_THROW(texture.getData(0, 1, &region, destination.data(), destination.size()),
                 System::NotSupportedException);
}

TEST(Texture2DArrayTest, TransferValidationRejectsInvalidRangesBeforeTheRenderer)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = std::make_unique<TextureArrayContractRenderer>(nativeDestructions);
    TextureArrayContractRenderer* const rendererView = renderer.get();
    renderer->colorSupport = {FullRequiredFormatUsages, FullRequiredFormatUsages};
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    Texture2DArray texture(
        device, Texture2DArrayDescriptor(7, 5, 3, 3, SurfaceFormat::Color, FullUsage));
    std::array<std::uint8_t, 140> bytes{};
    const Microsoft::Xna::Framework::Rectangle full(0, 0, 7, 5);
    const Microsoft::Xna::Framework::Rectangle negative(-1, 0, 1, 1);
    const Microsoft::Xna::Framework::Rectangle empty(0, 0, 0, 1);
    const Microsoft::Xna::Framework::Rectangle outside(6, 4, 2, 1);

    EXPECT_THROW(texture.setData(-1, 0, &full, bytes.data(), bytes.size()), std::out_of_range);
    EXPECT_THROW(texture.setData(3, 0, &full, bytes.data(), bytes.size()), std::out_of_range);
    EXPECT_THROW(texture.setData(0, -1, &full, bytes.data(), bytes.size()), std::out_of_range);
    EXPECT_THROW(texture.setData(0, 3, &full, bytes.data(), bytes.size()), std::out_of_range);
    EXPECT_THROW(texture.setData(0, 0, &negative, bytes.data(), 4), std::out_of_range);
    EXPECT_THROW(texture.setData(0, 0, &empty, bytes.data(), 0), std::out_of_range);
    EXPECT_THROW(texture.setData(0, 0, &outside, bytes.data(), 8), std::out_of_range);
    EXPECT_THROW(texture.setData(0, 0, &full, bytes.data(), bytes.size() - 1),
                 std::invalid_argument);
    EXPECT_THROW(texture.setData(0, 0, &full, nullptr, bytes.size()),
                 std::invalid_argument);
    EXPECT_EQ(rendererView->state->uploadCalls, 0);

    Texture2DArray sampledOnly(
        device, Texture2DArrayDescriptor(
            4, 4, 2, 1, SurfaceFormat::Color, Texture2DArrayUsage::Sampled));
    std::array<std::uint8_t, 64> level{};
    EXPECT_THROW(sampledOnly.setData(0, 0, nullptr, level.data(), level.size()),
                 System::NotSupportedException);
    EXPECT_THROW(sampledOnly.getData(0, 0, nullptr, level.data(), level.size()),
                 System::NotSupportedException);
    EXPECT_EQ(rendererView->state->uploadCalls, 0);
    EXPECT_EQ(rendererView->state->readbackCalls, 0);
}

TEST(Texture2DArrayTest, CompressedTransfersCountBlocksAndPermitOddMipEdges)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = std::make_unique<TextureArrayContractRenderer>(nativeDestructions);
    TextureArrayContractRenderer* const rendererView = renderer.get();
    renderer->supportAllFormats = true;
    renderer->colorSupport = {FullRequiredFormatUsages, FullRequiredFormatUsages};
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    Texture2DArray texture(
        device, Texture2DArrayDescriptor(7, 5, 2, 3, SurfaceFormat::Dxt1, FullUsage));
    std::array<std::uint8_t, 32> full{};
    texture.setData(0, 0, nullptr, full.data(), full.size());
    EXPECT_EQ(rendererView->state->uploadCalls, 1);

    const Microsoft::Xna::Framework::Rectangle unalignedOffset(2, 0, 4, 4);
    const Microsoft::Xna::Framework::Rectangle partialBlock(0, 0, 2, 4);
    EXPECT_THROW(texture.setData(0, 0, &unalignedOffset, full.data(), 8),
                 std::invalid_argument);
    EXPECT_THROW(texture.setData(0, 0, &partialBlock, full.data(), 8),
                 std::invalid_argument);
    EXPECT_EQ(rendererView->state->uploadCalls, 1);

    const Microsoft::Xna::Framework::Rectangle oddBottomRight(4, 4, 3, 1);
    texture.setData(1, 0, &oddBottomRight, full.data(), 8);
    EXPECT_EQ(rendererView->state->uploadCalls, 2);
    EXPECT_EQ(rendererView->state->upload.layer, 1);
    EXPECT_EQ(rendererView->state->upload.x, 4);
    EXPECT_EQ(rendererView->state->upload.y, 4);
    EXPECT_EQ(rendererView->state->upload.width, 3);
    EXPECT_EQ(rendererView->state->upload.height, 1);
    EXPECT_EQ(rendererView->state->upload.bytes.size(), 8U);
}

TEST(Texture2DArrayTest, ShaderBindingRetainsOnlyTheInternalRecordAndCanBeCleared)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = std::make_unique<TextureArrayContractRenderer>(nativeDestructions);
    TextureArrayContractRenderer* const rendererView = renderer.get();
    renderer->colorSupport = {FullRequiredFormatUsages, FullRequiredFormatUsages};
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    Microsoft::Xna::Framework::Graphics::ShaderEffect effect(device, "vertex", "fragment");
    auto texture = std::make_unique<Texture2DArray>(
        device, Texture2DArrayDescriptor(4, 4, 2, 1, SurfaceFormat::Color, FullUsage));
    effect.SetTextureArrayEXT(2, *texture);
    EXPECT_EQ(rendererView->state->bindingCalls, 1);
    EXPECT_EQ(rendererView->state->lastBindingUnit, 2);
    EXPECT_NE(rendererView->state->boundArray, nullptr);

    texture->Dispose();
    EXPECT_EQ(nativeDestructions, 0)
        << "the effect retains renderer work, never the disposed public resource";
    EXPECT_THROW(effect.SetTextureArrayEXT(0, *texture), System::ObjectDisposedException);
    effect.ClearTextureArrayEXT(2);
    EXPECT_EQ(rendererView->state->bindingCalls, 2);
    EXPECT_EQ(rendererView->state->boundArray, nullptr);
    EXPECT_EQ(nativeDestructions, 1);

    auto rejected = std::make_unique<Texture2DArray>(
        device, Texture2DArrayDescriptor(4, 4, 2, 1, SurfaceFormat::Color, FullUsage));
    rendererView->state->acceptBinding = false;
    EXPECT_THROW(effect.SetTextureArrayEXT(1, *rejected), System::NotSupportedException);
    EXPECT_THROW(effect.ClearTextureArrayEXT(1), System::NotSupportedException);
}

TEST(Texture2DArrayTest, TracksDisposesAndReleasesItsInternalRecordInDeviceOrder)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = std::make_unique<TextureArrayContractRenderer>(nativeDestructions);
    TextureArrayContractRenderer* const rendererView = renderer.get();
    renderer->colorSupport = {
        FullRequiredFormatUsages,
        FullRequiredFormatUsages};
    CNA::Internal::Texture2DArrayGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));

    const Texture2DArrayDescriptor descriptor(
        7, 5, 3, 3, SurfaceFormat::Color, FullUsage);
    const std::size_t baseline = device.GetTrackedResourceCount();
    int resourceCreations = 0;
    int resourceDestructions = 0;
    int disposingEvents = 0;

    device.ResourceCreated +=
        [&](System::Object*, const ResourceCreatedEventArgs& args)
        {
            EXPECT_NE(args.getResourceProperty(), nullptr);
            ++resourceCreations;
        };
    device.ResourceDestroyed +=
        [&](System::Object*, const ResourceDestroyedEventArgs&)
        {
            ++resourceDestructions;
        };

    auto texture = std::make_unique<Texture2DArray>(device, descriptor);
    texture->Disposing +=
        [&](System::Object*, const System::EventArgs&) { ++disposingEvents; };

    ASSERT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    EXPECT_EQ(resourceCreations, 1);
    EXPECT_FALSE(texture->getIsDisposedProperty());
    EXPECT_EQ(texture->getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(texture->getDescriptor().getWidth(), 7);
    EXPECT_EQ(texture->GetTypeName(), "CNA.Graphics.Texture2DArray");
    EXPECT_EQ(rendererView->factoryCalls, 1);
    EXPECT_EQ(rendererView->createdWidth, 7);
    EXPECT_EQ(rendererView->createdHeight, 5);
    EXPECT_EQ(rendererView->createdLayers, 3);
    EXPECT_EQ(rendererView->createdMips, 3);
    EXPECT_EQ(rendererView->createdFormat, static_cast<int>(SurfaceFormat::Color));
    EXPECT_EQ(rendererView->createdUsage, static_cast<std::uint32_t>(FullUsage));

    texture->Dispose();
    EXPECT_TRUE(texture->getIsDisposedProperty());
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(resourceDestructions, 1);
    EXPECT_EQ(disposingEvents, 1);
    EXPECT_THROW((void)texture->getDescriptor(), System::ObjectDisposedException);

    texture->Dispose();
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(resourceDestructions, 1);
    EXPECT_EQ(disposingEvents, 1);

    auto deviceOwned = std::make_unique<Texture2DArray>(device, descriptor);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    EXPECT_EQ(resourceCreations, 2);

    device.Dispose();
    EXPECT_TRUE(deviceOwned->getIsDisposedProperty());
    EXPECT_EQ(device.GetTrackedResourceCount(), 0U);
    EXPECT_EQ(nativeDestructions, 2);
    EXPECT_EQ(resourceDestructions, 2);
    EXPECT_THROW((void)deviceOwned->getDescriptor(), System::ObjectDisposedException);
    EXPECT_THROW(Texture2DArray(device, descriptor), System::ObjectDisposedException);
}

#endif // CNA_CNAEXT
