// SPDX-License-Identifier: MS-PL

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/Texture2DArray.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

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
    class RecordingTexture2DArrayRenderer final
        : public CNA::Internal::Renderers::ITexture2DArrayRenderer
    {
    public:
        explicit RecordingTexture2DArrayRenderer(int& destructionCount)
            : destructionCount_(destructionCount)
        {
        }

        ~RecordingTexture2DArrayRenderer() override { ++destructionCount_; }

    private:
        int& destructionCount_;
    };

    class TextureArrayContractRenderer final
        : public CNA::Internal::Renderers::IGraphicsRenderer
    {
    public:
        explicit TextureArrayContractRenderer(int& nativeDestructions)
            : nativeDestructions_(&nativeDestructions)
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
            return surfaceFormat == static_cast<int>(SurfaceFormat::Color)
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
            return std::make_unique<RecordingTexture2DArrayRenderer>(*nativeDestructions_);
        }

        int maxDimension = 8;
        int maxLayers = 3;
        CNA::RendererFormatSupport colorSupport{};
        bool createResource = true;
        int factoryCalls = 0;
        int createdWidth = 0;
        int createdHeight = 0;
        int createdLayers = 0;
        int createdMips = 0;
        int createdFormat = -1;
        std::uint32_t createdUsage = 0;

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
