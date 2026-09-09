// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/StorageTexture2D.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Graphics
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        constexpr std::uint32_t AllowedStorageTexture2DUsages =
            static_cast<std::uint32_t>(StorageTexture2DUsage::StorageRead) |
            static_cast<std::uint32_t>(StorageTexture2DUsage::StorageWrite) |
            static_cast<std::uint32_t>(StorageTexture2DUsage::Sampled) |
            static_cast<std::uint32_t>(StorageTexture2DUsage::Filterable) |
            static_cast<std::uint32_t>(StorageTexture2DUsage::TransferSource) |
            static_cast<std::uint32_t>(StorageTexture2DUsage::TransferDestination);

        constexpr std::uint32_t StorageAccessUsages =
            static_cast<std::uint32_t>(StorageTexture2DUsage::StorageRead) |
            static_cast<std::uint32_t>(StorageTexture2DUsage::StorageWrite);

        [[nodiscard]] int CompleteMipLevelCount(int width, int height) noexcept
        {
            int levels = 1;
            while (width > 1 || height > 1)
            {
                width = std::max(1, width / 2);
                height = std::max(1, height / 2);
                ++levels;
            }
            return levels;
        }

        [[nodiscard]] std::uint32_t RequiredFormatUsages(
            const StorageTexture2DDescriptor& descriptor) noexcept
        {
            std::uint32_t required =
                static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage);
            const std::uint32_t usage = static_cast<std::uint32_t>(descriptor.getUsage());
            if ((usage & static_cast<std::uint32_t>(StorageTexture2DUsage::StorageRead)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead);
            if ((usage & static_cast<std::uint32_t>(StorageTexture2DUsage::StorageWrite)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite);
            if ((usage & static_cast<std::uint32_t>(StorageTexture2DUsage::Sampled)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled);
            if ((usage & static_cast<std::uint32_t>(StorageTexture2DUsage::Filterable)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::Filterable);
            if ((usage & static_cast<std::uint32_t>(StorageTexture2DUsage::TransferSource)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource);
            if ((usage & static_cast<std::uint32_t>(StorageTexture2DUsage::TransferDestination)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination);
            if (descriptor.getMipLevelCount() > 1)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped);
            return required;
        }

        [[noreturn]] void ThrowLimitUnavailable(
            const GraphicsDevice& device, const std::string& limit)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: the '" +
                std::string(device.GetGraphicsRendererName()) +
                "' renderer exposes no implemented " + limit + " for storage textures");
        }

        struct TransferRegion
        {
            int x;
            int y;
            int width;
            int height;
            std::size_t byteCount;
        };

        [[nodiscard]] std::size_t CheckedMultiply(
            const std::size_t left, const std::size_t right)
        {
            if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right)
                throw std::invalid_argument(
                    "CNA::Graphics::StorageTexture2D: transfer byte count overflows size_t");
            return left * right;
        }

        [[nodiscard]] TransferRegion ValidateTransfer(
            const StorageTexture2DDescriptor& descriptor, const int mipLevel,
            const Microsoft::Xna::Framework::Rectangle* rectangle, const void* data,
            const std::size_t byteCount)
        {
            if (mipLevel < 0 || mipLevel >= descriptor.getMipLevelCount())
                throw std::out_of_range(
                    "CNA::Graphics::StorageTexture2D: mipLevel is outside the allocated chain");

            int mipWidth = descriptor.getWidth();
            int mipHeight = descriptor.getHeight();
            for (int level = 0; level < mipLevel; ++level)
            {
                mipWidth = std::max(1, mipWidth / 2);
                mipHeight = std::max(1, mipHeight / 2);
            }

            const TransferRegion requested{
                rectangle != nullptr ? rectangle->X : 0,
                rectangle != nullptr ? rectangle->Y : 0,
                rectangle != nullptr ? rectangle->Width : mipWidth,
                rectangle != nullptr ? rectangle->Height : mipHeight,
                0};
            if (requested.x < 0 || requested.y < 0 || requested.width <= 0 ||
                requested.height <= 0 || requested.x > mipWidth - requested.width ||
                requested.y > mipHeight - requested.height)
            {
                throw std::out_of_range(
                    "CNA::Graphics::StorageTexture2D: rectangle is outside the selected mip level");
            }

            const auto format = descriptor.getFormat();
            const int blockSquared =
                Microsoft::Xna::Framework::Graphics::Texture::GetBlockSizeSquaredEXT(format);
            const int blockExtent = blockSquared == 1 ? 1 : 4;
            if (blockExtent > 1 &&
                ((requested.x % blockExtent) != 0 || (requested.y % blockExtent) != 0 ||
                 ((requested.width % blockExtent) != 0 &&
                  requested.x + requested.width != mipWidth) ||
                 ((requested.height % blockExtent) != 0 &&
                  requested.y + requested.height != mipHeight)))
            {
                throw std::invalid_argument(
                    "CNA::Graphics::StorageTexture2D: compressed rectangles must begin on a 4x4 "
                    "block boundary and end on a block or mip boundary");
            }

            const std::size_t blockWidth =
                (static_cast<std::size_t>(requested.width) + blockExtent - 1) / blockExtent;
            const std::size_t blockHeight =
                (static_cast<std::size_t>(requested.height) + blockExtent - 1) / blockExtent;
            const std::size_t expected = CheckedMultiply(
                CheckedMultiply(blockWidth, blockHeight),
                static_cast<std::size_t>(
                    Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format)));
            if (byteCount != expected)
                throw std::invalid_argument(
                    "CNA::Graphics::StorageTexture2D: byteCount " +
                    std::to_string(byteCount) + " does not exactly match the addressed " +
                    std::to_string(expected) + " bytes");
            if (data == nullptr)
                throw std::invalid_argument(
                    "CNA::Graphics::StorageTexture2D: data must not be null");
            return {requested.x, requested.y, requested.width, requested.height, expected};
        }
    }

    StorageTexture2DDescriptor::StorageTexture2DDescriptor(
        const int width, const int height, const int mipLevelCount,
        const SurfaceFormat format, const StorageTexture2DUsage usage)
        : width_(width)
        , height_(height)
        , mipLevelCount_(mipLevelCount)
        , format_(format)
        , usage_(usage)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: width and height must be positive");
        if (mipLevelCount <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: mipLevelCount must be positive");
        const int completeMipLevels = CompleteMipLevelCount(width, height);
        if (mipLevelCount > completeMipLevels)
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: mipLevelCount " +
                std::to_string(mipLevelCount) + " exceeds the complete " +
                std::to_string(completeMipLevels) + "-level chain for " +
                std::to_string(width) + "x" + std::to_string(height));

        const int formatOrdinal = static_cast<int>(format);
        if (formatOrdinal < static_cast<int>(SurfaceFormat::Color) ||
            formatOrdinal > static_cast<int>(SurfaceFormat::UShortEXT))
        {
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: format is not a declared SurfaceFormat");
        }

        const std::uint32_t usageBits = static_cast<std::uint32_t>(usage);
        if ((usageBits & ~AllowedStorageTexture2DUsages) != 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: usage contains an unknown bit");
        if ((usageBits & StorageAccessUsages) == 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: StorageRead or StorageWrite is required");
        if ((usageBits & static_cast<std::uint32_t>(StorageTexture2DUsage::Filterable)) != 0 &&
            (usageBits & static_cast<std::uint32_t>(StorageTexture2DUsage::Sampled)) == 0)
        {
            throw std::invalid_argument(
                "CNA::Graphics::StorageTexture2DDescriptor: Filterable requires Sampled usage");
        }
    }

    int StorageTexture2DDescriptor::getWidth() const noexcept { return width_; }
    int StorageTexture2DDescriptor::getHeight() const noexcept { return height_; }
    int StorageTexture2DDescriptor::getMipLevelCount() const noexcept { return mipLevelCount_; }
    SurfaceFormat StorageTexture2DDescriptor::getFormat() const noexcept { return format_; }
    StorageTexture2DUsage StorageTexture2DDescriptor::getUsage() const noexcept { return usage_; }

    StorageTexture2D::Prepared StorageTexture2D::prepare(
        GraphicsDevice& device, const StorageTexture2DDescriptor& descriptor)
    {
        if (device.getIsDisposedProperty())
            throw System::ObjectDisposedException("GraphicsDevice");

        const CNA::RendererLimitValue maxDimension =
            device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureDimension);
        if (!maxDimension.known || maxDimension.value == 0)
            ThrowLimitUnavailable(device, "maximum texture dimension");
        if (static_cast<std::uint64_t>(descriptor.getWidth()) > maxDimension.value ||
            static_cast<std::uint64_t>(descriptor.getHeight()) > maxDimension.value)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: " +
                std::to_string(descriptor.getWidth()) + "x" +
                std::to_string(descriptor.getHeight()) +
                " exceeds the device's maximum texture dimension of " +
                std::to_string(maxDimension.value));
        }

        const CNA::RendererLimitValue maxStorageImages =
            device.GetRendererLimitEXT(CNA::RendererLimit::MaxStorageImagesPerShaderStage);
        if (!maxStorageImages.known || maxStorageImages.value == 0)
            ThrowLimitUnavailable(device, "storage-image binding count");

        const std::uint32_t requiredUsages = RequiredFormatUsages(descriptor);
        const CNA::RendererFormatSupport formatSupport =
            device.GetRendererSurfaceFormatSupportEXT(descriptor.getFormat());
        if (!formatSupport.Supports(static_cast<CNA::RendererFormatUsage>(requiredUsages)))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: SurfaceFormat " +
                std::to_string(static_cast<int>(descriptor.getFormat())) +
                " does not have every known-and-supported usage required by this descriptor on " +
                std::string(device.GetGraphicsRendererName()));
        }

        std::unique_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer> native =
            device.GetRenderer().CreateStorageTexture2DEXT(
                descriptor.getWidth(), descriptor.getHeight(), descriptor.getMipLevelCount(),
                static_cast<int>(descriptor.getFormat()),
                static_cast<std::uint32_t>(descriptor.getUsage()));
        if (native == nullptr)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: the '" +
                std::string(device.GetGraphicsRendererName()) +
                "' renderer advertised the descriptor's limits and format usages but did not "
                "create a storage texture");
        }

        return Prepared{descriptor,
                        std::shared_ptr<CNA::Internal::Renderers::IStorageTexture2DRenderer>(
                            std::move(native))};
    }

    StorageTexture2D::StorageTexture2D(
        GraphicsDevice& device, const StorageTexture2DDescriptor& descriptor)
        : StorageTexture2D(device, prepare(device, descriptor))
    {
    }

    StorageTexture2D::StorageTexture2D(GraphicsDevice& device, Prepared prepared)
        : GraphicsResource(&device)
        , descriptor_(std::move(prepared.descriptor))
        , renderer_(std::move(prepared.renderer))
    {
    }

    StorageTexture2D::~StorageTexture2D() = default;

    const StorageTexture2DDescriptor& StorageTexture2D::getDescriptor() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageTexture2D");
        return descriptor_;
    }

    void StorageTexture2D::setData(
        const int mipLevel, const Microsoft::Xna::Framework::Rectangle* rectangle,
        const void* data, const std::size_t byteCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageTexture2D");
        if ((descriptor_.getUsage() & StorageTexture2DUsage::TransferDestination) ==
            StorageTexture2DUsage::None)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: TransferDestination usage was not declared");
        }
        const TransferRegion region =
            ValidateTransfer(descriptor_, mipLevel, rectangle, data, byteCount);
        if (renderer_ == nullptr ||
            !renderer_->SetData(mipLevel, region.x, region.y, region.width, region.height,
                                data, region.byteCount))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: the renderer refused the complete upload");
        }
    }

    void StorageTexture2D::getData(
        const int mipLevel, const Microsoft::Xna::Framework::Rectangle* rectangle,
        void* data, const std::size_t byteCount) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageTexture2D");
        if ((descriptor_.getUsage() & StorageTexture2DUsage::TransferSource) ==
            StorageTexture2DUsage::None)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: TransferSource usage was not declared");
        }
        const TransferRegion region =
            ValidateTransfer(descriptor_, mipLevel, rectangle, data, byteCount);
        if (renderer_ == nullptr ||
            !renderer_->GetData(mipLevel, region.x, region.y, region.width, region.height,
                                data, region.byteCount))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageTexture2D: the renderer refused the complete readback");
        }
    }

    const std::string& StorageTexture2D::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.StorageTexture2D";
        return name;
    }

    void StorageTexture2D::Dispose(const bool disposing)
    {
        if (getIsDisposedProperty()) return;
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }
}

#endif // CNA_CNAEXT
