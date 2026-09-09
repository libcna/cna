// SPDX-License-Identifier: MS-PL

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
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

using CNA::Graphics::StorageTexture2D;
using CNA::Graphics::StorageTexture2DDescriptor;
using CNA::Graphics::StorageTexture2DUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace CNA::Internal
{
    class StorageTexture2DGraphicsDeviceTestPeer
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
        int mipLevel = -1;
        int x = -1;
        int y = -1;
        int width = -1;
        int height = -1;
        std::vector<std::uint8_t> bytes;
    };

    struct StorageTextureTestState
    {
        bool acceptUpload = true;
        bool acceptReadback = true;
        int uploadCalls = 0;
        int readbackCalls = 0;
        TransferCall upload;
        TransferCall readback;
        std::vector<std::uint8_t> readbackBytes;
    };

    class RecordingStorageTexture2DRenderer final
        : public CNA::Internal::Renderers::IStorageTexture2DRenderer
    {
    public:
        RecordingStorageTexture2DRenderer(
            int& destructionCount, const std::shared_ptr<StorageTextureTestState>& state)
            : destructionCount_(destructionCount)
            , state_(state)
        {
        }

        ~RecordingStorageTexture2DRenderer() override { ++destructionCount_; }

        [[nodiscard]] bool SetData(
            const int mipLevel, const int x, const int y, const int width, const int height,
            const void* data, const std::size_t byteCount) override
        {
            ++state_->uploadCalls;
            state_->upload = {mipLevel, x, y, width, height, {}};
            const auto* begin = static_cast<const std::uint8_t*>(data);
            state_->upload.bytes.assign(begin, begin + byteCount);
            return state_->acceptUpload;
        }

        [[nodiscard]] bool GetData(
            const int mipLevel, const int x, const int y, const int width, const int height,
            void* data, const std::size_t byteCount) const override
        {
            ++state_->readbackCalls;
            state_->readback = {mipLevel, x, y, width, height, {}};
            if (!state_->acceptReadback || state_->readbackBytes.size() != byteCount)
                return false;
            std::memcpy(data, state_->readbackBytes.data(), byteCount);
            state_->readback.bytes = state_->readbackBytes;
            return true;
        }

    private:
        int& destructionCount_;
        std::shared_ptr<StorageTextureTestState> state_;
    };

    class StorageTextureContractRenderer final
        : public CNA::Internal::Renderers::IGraphicsRenderer
    {
    public:
        explicit StorageTextureContractRenderer(int& nativeDestructions)
            : nativeDestructions_(&nativeDestructions)
            , state(std::make_shared<StorageTextureTestState>())
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
        [[nodiscard]] int GetMaxStorageImagesPerShaderStageEXT() const override
        {
            return maxStorageImages;
        }
        [[nodiscard]] CNA::RendererFormatSupport GetSurfaceFormatUsageSupportEXT(
            const int surfaceFormat) const override
        {
            return (supportAllFormats || surfaceFormat == static_cast<int>(SurfaceFormat::Color))
                ? formatSupport : CNA::RendererFormatSupport{};
        }
        std::unique_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer>
        CreateStorageTexture2DEXT(
            const int width, const int height, const int mipLevelCount,
            const int surfaceFormat, const std::uint32_t usage) override
        {
            ++factoryCalls;
            createdWidth = width;
            createdHeight = height;
            createdMips = mipLevelCount;
            createdFormat = surfaceFormat;
            createdUsage = usage;
            if (!createResource) return nullptr;
            return std::make_unique<RecordingStorageTexture2DRenderer>(
                *nativeDestructions_, state);
        }

        int maxDimension = 8;
        int maxStorageImages = 2;
        CNA::RendererFormatSupport formatSupport{};
        bool createResource = true;
        bool supportAllFormats = false;
        int factoryCalls = 0;
        int createdWidth = 0;
        int createdHeight = 0;
        int createdMips = 0;
        int createdFormat = -1;
        std::uint32_t createdUsage = 0;
        std::shared_ptr<StorageTextureTestState> state;

    private:
        int* nativeDestructions_;
    };

    constexpr StorageTexture2DUsage FullUsage =
        StorageTexture2DUsage::StorageRead |
        StorageTexture2DUsage::StorageWrite |
        StorageTexture2DUsage::Sampled |
        StorageTexture2DUsage::Filterable |
        StorageTexture2DUsage::TransferSource |
        StorageTexture2DUsage::TransferDestination;

    [[nodiscard]] constexpr bool HasUsage(
        const StorageTexture2DUsage value, const StorageTexture2DUsage flag)
    {
        return (value & flag) == flag;
    }

    constexpr std::uint32_t FullRequiredFormatUsages =
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Filterable) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped);

    [[nodiscard]] std::unique_ptr<StorageTextureContractRenderer> MakeRenderer(
        int& nativeDestructions)
    {
        auto renderer = std::make_unique<StorageTextureContractRenderer>(nativeDestructions);
        renderer->formatSupport = {FullRequiredFormatUsages, FullRequiredFormatUsages};
        return renderer;
    }
}

static_assert(std::is_base_of_v<
              Microsoft::Xna::Framework::Graphics::GraphicsResource, StorageTexture2D>);
static_assert(!std::is_copy_constructible_v<StorageTexture2D>);
static_assert(!std::is_copy_assignable_v<StorageTexture2D>);
static_assert(!std::is_move_constructible_v<StorageTexture2D>);
static_assert(!std::is_move_assignable_v<StorageTexture2D>);

TEST(StorageTexture2DDescriptorTest, RetainsEveryFieldAndComposesEveryUsageFlag)
{
    const StorageTexture2DDescriptor descriptor(
        7, 5, 3, SurfaceFormat::HalfVector4, FullUsage);

    EXPECT_EQ(descriptor.getWidth(), 7);
    EXPECT_EQ(descriptor.getHeight(), 5);
    EXPECT_EQ(descriptor.getMipLevelCount(), 3);
    EXPECT_EQ(descriptor.getFormat(), SurfaceFormat::HalfVector4);
    EXPECT_EQ(descriptor.getUsage(), FullUsage);
    EXPECT_TRUE(HasUsage(FullUsage, StorageTexture2DUsage::StorageRead));
    EXPECT_TRUE(HasUsage(FullUsage, StorageTexture2DUsage::StorageWrite));
    EXPECT_TRUE(HasUsage(FullUsage, StorageTexture2DUsage::Sampled));
    EXPECT_TRUE(HasUsage(FullUsage, StorageTexture2DUsage::Filterable));
    EXPECT_TRUE(HasUsage(FullUsage, StorageTexture2DUsage::TransferSource));
    EXPECT_TRUE(HasUsage(FullUsage, StorageTexture2DUsage::TransferDestination));
    EXPECT_EQ(StorageTexture2DUsage::StorageRead & StorageTexture2DUsage::StorageWrite,
              StorageTexture2DUsage::None);
}

TEST(StorageTexture2DDescriptorTest, RejectsEveryIntrinsicInvalidDescription)
{
    const auto storage = StorageTexture2DUsage::StorageRead;
    EXPECT_THROW(StorageTexture2DDescriptor(0, 4, 1, SurfaceFormat::Color, storage),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(4, 0, 1, SurfaceFormat::Color, storage),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(4, 4, 0, SurfaceFormat::Color, storage),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(7, 5, 4, SurfaceFormat::Color, storage),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(
                     4, 4, 1, static_cast<SurfaceFormat>(-1), storage),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(
                     4, 4, 1, static_cast<SurfaceFormat>(27), storage),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(
                     4, 4, 1, SurfaceFormat::Color, StorageTexture2DUsage::None),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(
                     4, 4, 1, SurfaceFormat::Color, StorageTexture2DUsage::Sampled),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(
                     4, 4, 1, SurfaceFormat::Color,
                     StorageTexture2DUsage::StorageRead | StorageTexture2DUsage::Filterable),
                 std::invalid_argument);
    EXPECT_THROW(StorageTexture2DDescriptor(
                     4, 4, 1, SurfaceFormat::Color,
                     static_cast<StorageTexture2DUsage>(UINT32_C(1) << 31)),
                 std::invalid_argument);
    EXPECT_NO_THROW(StorageTexture2DDescriptor(
        4, 4, 1, SurfaceFormat::Color, StorageTexture2DUsage::StorageWrite));
}

TEST(StorageTexture2DTest, LiveConstructionRequiresTheWholePublishedDeviceContract)
{
    GraphicsDevice device;
    const StorageTexture2DDescriptor descriptor(
        4, 4, 1, SurfaceFormat::Color, StorageTexture2DUsage::StorageWrite);
    const std::size_t baseline = device.GetTrackedResourceCount();
    const CNA::RendererLimitValue dimension =
        device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureDimension);
    const CNA::RendererLimitValue images =
        device.GetRendererLimitEXT(CNA::RendererLimit::MaxStorageImagesPerShaderStage);
    const auto required = static_cast<CNA::RendererFormatUsage>(
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite));
    const bool advertised = dimension.known && dimension.value >= 4 &&
        images.known && images.value > 0 &&
        device.GetRendererSurfaceFormatSupportEXT(SurfaceFormat::Color).Supports(required);

    if (advertised)
    {
        const StorageTexture2D texture(device, descriptor);
        EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    }
    else
    {
        EXPECT_THROW(StorageTexture2D(device, descriptor), System::NotSupportedException);
        EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
    }
}

TEST(StorageTexture2DTest, ValidatesLimitsAndFormatUsageBeforeRendererCreation)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = MakeRenderer(nativeDestructions);
    StorageTextureContractRenderer* const view = renderer.get();
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    const std::size_t baseline = device.GetTrackedResourceCount();

    EXPECT_THROW(StorageTexture2D(
                     device, StorageTexture2DDescriptor(
                         9, 5, 1, SurfaceFormat::Color, StorageTexture2DUsage::StorageRead)),
                 System::NotSupportedException);
    EXPECT_EQ(view->factoryCalls, 0);

    view->maxStorageImages = 0;
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(StorageTexture2D(
                     device, StorageTexture2DDescriptor(
                         7, 5, 1, SurfaceFormat::Color, StorageTexture2DUsage::StorageRead)),
                 System::NotSupportedException);
    EXPECT_EQ(view->factoryCalls, 0);

    view->maxStorageImages = 2;
    view->formatSupport.supportedUsages &=
        ~static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite);
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    const StorageTexture2DDescriptor full(7, 5, 3, SurfaceFormat::Color, FullUsage);
    EXPECT_THROW(StorageTexture2D(device, full), System::NotSupportedException);
    EXPECT_EQ(view->factoryCalls, 0);

    view->formatSupport.supportedUsages = FullRequiredFormatUsages;
    view->createResource = false;
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::InvalidateCapabilityProfile(device);
    EXPECT_THROW(StorageTexture2D(device, full), System::NotSupportedException);
    EXPECT_EQ(view->factoryCalls, 1);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);

    view->createResource = true;
    {
        StorageTexture2D texture(device, full);
        EXPECT_EQ(view->factoryCalls, 2);
        EXPECT_EQ(view->createdWidth, 7);
        EXPECT_EQ(view->createdHeight, 5);
        EXPECT_EQ(view->createdMips, 3);
        EXPECT_EQ(view->createdFormat, static_cast<int>(SurfaceFormat::Color));
        EXPECT_EQ(view->createdUsage, static_cast<std::uint32_t>(FullUsage));
        EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    }
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
}

TEST(StorageTexture2DTest, TransfersPreserveMipRectangleAndExactBytes)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = MakeRenderer(nativeDestructions);
    StorageTextureContractRenderer* const view = renderer.get();
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    StorageTexture2D texture(
        device, StorageTexture2DDescriptor(7, 5, 3, SurfaceFormat::Color, FullUsage));

    const Microsoft::Xna::Framework::Rectangle region(1, 0, 2, 2);
    const std::array<std::uint8_t, 16> source{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    texture.setData(1, &region, source.data(), source.size());
    EXPECT_EQ(view->state->uploadCalls, 1);
    EXPECT_EQ(view->state->upload.mipLevel, 1);
    EXPECT_EQ(view->state->upload.x, 1);
    EXPECT_EQ(view->state->upload.y, 0);
    EXPECT_EQ(view->state->upload.width, 2);
    EXPECT_EQ(view->state->upload.height, 2);
    EXPECT_EQ(view->state->upload.bytes,
              std::vector<std::uint8_t>(source.begin(), source.end()));

    view->state->readbackBytes.assign(source.rbegin(), source.rend());
    std::array<std::uint8_t, 16> destination{};
    texture.getData(1, &region, destination.data(), destination.size());
    EXPECT_EQ(view->state->readbackCalls, 1);
    EXPECT_EQ(std::vector<std::uint8_t>(destination.begin(), destination.end()),
              view->state->readbackBytes);

    view->state->acceptUpload = false;
    EXPECT_THROW(texture.setData(1, &region, source.data(), source.size()),
                 System::NotSupportedException);
    view->state->acceptReadback = false;
    EXPECT_THROW(texture.getData(1, &region, destination.data(), destination.size()),
                 System::NotSupportedException);
}

TEST(StorageTexture2DTest, TransferValidationRefusesInvalidAndUndeclaredAccess)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = MakeRenderer(nativeDestructions);
    StorageTextureContractRenderer* const view = renderer.get();
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    StorageTexture2D texture(
        device, StorageTexture2DDescriptor(7, 5, 3, SurfaceFormat::Color, FullUsage));
    std::array<std::uint8_t, 140> bytes{};
    const Microsoft::Xna::Framework::Rectangle full(0, 0, 7, 5);
    const Microsoft::Xna::Framework::Rectangle negative(-1, 0, 1, 1);
    const Microsoft::Xna::Framework::Rectangle empty(0, 0, 0, 1);
    const Microsoft::Xna::Framework::Rectangle outside(6, 4, 2, 1);

    EXPECT_THROW(texture.setData(-1, &full, bytes.data(), bytes.size()), std::out_of_range);
    EXPECT_THROW(texture.setData(3, &full, bytes.data(), bytes.size()), std::out_of_range);
    EXPECT_THROW(texture.setData(0, &negative, bytes.data(), 4), std::out_of_range);
    EXPECT_THROW(texture.setData(0, &empty, bytes.data(), 0), std::out_of_range);
    EXPECT_THROW(texture.setData(0, &outside, bytes.data(), 8), std::out_of_range);
    EXPECT_THROW(texture.setData(0, &full, bytes.data(), bytes.size() - 1),
                 std::invalid_argument);
    EXPECT_THROW(texture.setData(0, &full, nullptr, bytes.size()), std::invalid_argument);
    EXPECT_EQ(view->state->uploadCalls, 0);

    StorageTexture2D storageOnly(
        device, StorageTexture2DDescriptor(
            4, 4, 1, SurfaceFormat::Color, StorageTexture2DUsage::StorageRead));
    std::array<std::uint8_t, 64> level{};
    EXPECT_THROW(storageOnly.setData(0, nullptr, level.data(), level.size()),
                 System::NotSupportedException);
    EXPECT_THROW(storageOnly.getData(0, nullptr, level.data(), level.size()),
                 System::NotSupportedException);
    EXPECT_EQ(view->state->uploadCalls, 0);
    EXPECT_EQ(view->state->readbackCalls, 0);
}

TEST(StorageTexture2DTest, CompressedTransferValidationCountsBlocksAndOddEdges)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = MakeRenderer(nativeDestructions);
    StorageTextureContractRenderer* const view = renderer.get();
    view->supportAllFormats = true;
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    StorageTexture2D texture(
        device, StorageTexture2DDescriptor(7, 5, 3, SurfaceFormat::Dxt1, FullUsage));
    std::array<std::uint8_t, 32> bytes{};
    texture.setData(0, nullptr, bytes.data(), bytes.size());
    EXPECT_EQ(view->state->uploadCalls, 1);

    const Microsoft::Xna::Framework::Rectangle unaligned(2, 0, 4, 4);
    const Microsoft::Xna::Framework::Rectangle partial(0, 0, 2, 4);
    EXPECT_THROW(texture.setData(0, &unaligned, bytes.data(), 8), std::invalid_argument);
    EXPECT_THROW(texture.setData(0, &partial, bytes.data(), 8), std::invalid_argument);
    EXPECT_EQ(view->state->uploadCalls, 1);

    const Microsoft::Xna::Framework::Rectangle oddEdge(4, 4, 3, 1);
    texture.setData(0, &oddEdge, bytes.data(), 8);
    EXPECT_EQ(view->state->uploadCalls, 2);
    EXPECT_EQ(view->state->upload.x, 4);
    EXPECT_EQ(view->state->upload.y, 4);
    EXPECT_EQ(view->state->upload.bytes.size(), 8U);
}

TEST(StorageTexture2DTest, TracksDisposesAndReleasesItsRendererRecordInDeviceOrder)
{
    GraphicsDevice device;
    int nativeDestructions = 0;
    auto renderer = MakeRenderer(nativeDestructions);
    CNA::Internal::StorageTexture2DGraphicsDeviceTestPeer::ReplaceRenderer(
        device, std::move(renderer));
    const StorageTexture2DDescriptor descriptor(
        7, 5, 3, SurfaceFormat::Color, FullUsage);
    const std::size_t baseline = device.GetTrackedResourceCount();
    int creations = 0;
    int destructions = 0;
    int disposingEvents = 0;

    device.ResourceCreated +=
        [&](System::Object*, const ResourceCreatedEventArgs& args)
        {
            EXPECT_NE(args.getResourceProperty(), nullptr);
            ++creations;
        };
    device.ResourceDestroyed +=
        [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destructions; };

    auto texture = std::make_unique<StorageTexture2D>(device, descriptor);
    texture->Disposing +=
        [&](System::Object*, const System::EventArgs&) { ++disposingEvents; };
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    EXPECT_EQ(texture->getDescriptor().getWidth(), 7);
    EXPECT_EQ(texture->GetTypeName(), "CNA.Graphics.StorageTexture2D");
    EXPECT_EQ(creations, 1);

    texture->Dispose();
    EXPECT_TRUE(texture->getIsDisposedProperty());
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(disposingEvents, 1);
    EXPECT_THROW((void)texture->getDescriptor(), System::ObjectDisposedException);
    texture->Dispose();
    EXPECT_EQ(nativeDestructions, 1);
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(disposingEvents, 1);

    auto deviceOwned = std::make_unique<StorageTexture2D>(device, descriptor);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    device.Dispose();
    EXPECT_TRUE(deviceOwned->getIsDisposedProperty());
    EXPECT_EQ(device.GetTrackedResourceCount(), 0U);
    EXPECT_EQ(nativeDestructions, 2);
    EXPECT_EQ(destructions, 2);
    EXPECT_THROW((void)deviceOwned->getDescriptor(), System::ObjectDisposedException);
    EXPECT_THROW(StorageTexture2D(device, descriptor), System::ObjectDisposedException);
}

#endif // CNA_CNAEXT
