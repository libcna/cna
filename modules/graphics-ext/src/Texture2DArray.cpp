// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/Texture2DArray.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
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
        constexpr std::uint32_t AllowedTexture2DArrayUsages =
            static_cast<std::uint32_t>(Texture2DArrayUsage::Sampled) |
            static_cast<std::uint32_t>(Texture2DArrayUsage::Filterable) |
            static_cast<std::uint32_t>(Texture2DArrayUsage::TransferSource) |
            static_cast<std::uint32_t>(Texture2DArrayUsage::TransferDestination);

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
            const Texture2DArrayDescriptor& descriptor) noexcept
        {
            std::uint32_t required =
                static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
                static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled);
            const std::uint32_t usage = static_cast<std::uint32_t>(descriptor.getUsage());
            if ((usage & static_cast<std::uint32_t>(Texture2DArrayUsage::Filterable)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::Filterable);
            if ((usage & static_cast<std::uint32_t>(Texture2DArrayUsage::TransferSource)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource);
            if ((usage & static_cast<std::uint32_t>(Texture2DArrayUsage::TransferDestination)) != 0)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination);
            if (descriptor.getMipLevelCount() > 1)
                required |= static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped);
            return required;
        }

        [[noreturn]] void ThrowLimitUnavailable(
            const GraphicsDevice& device, const std::string& limit)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: the '" +
                std::string(device.GetGraphicsRendererName()) +
                "' renderer exposes no implemented " + limit + " for texture arrays");
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
                    "CNA::Graphics::Texture2DArray: transfer byte count overflows size_t");
            return left * right;
        }

        [[nodiscard]] TransferRegion ValidateTransfer(
            const Texture2DArrayDescriptor& descriptor, const int layer, const int mipLevel,
            const Microsoft::Xna::Framework::Rectangle* rectangle, const void* data,
            const std::size_t byteCount)
        {
            if (layer < 0 || layer >= descriptor.getLayerCount())
                throw std::out_of_range(
                    "CNA::Graphics::Texture2DArray: layer is outside the array");
            if (mipLevel < 0 || mipLevel >= descriptor.getMipLevelCount())
                throw std::out_of_range(
                    "CNA::Graphics::Texture2DArray: mipLevel is outside the allocated chain");

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
                    "CNA::Graphics::Texture2DArray: rectangle is outside the selected mip level");
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
                    "CNA::Graphics::Texture2DArray: compressed rectangles must begin on a 4x4 "
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
                    "CNA::Graphics::Texture2DArray: byteCount " +
                    std::to_string(byteCount) + " does not exactly match the addressed " +
                    std::to_string(expected) + " bytes");
            if (data == nullptr)
                throw std::invalid_argument(
                    "CNA::Graphics::Texture2DArray: data must not be null");
            return {requested.x, requested.y, requested.width, requested.height, expected};
        }
    }

    Texture2DArrayDescriptor::Texture2DArrayDescriptor(
        const int width, const int height, const int layerCount, const int mipLevelCount,
        const SurfaceFormat format, const Texture2DArrayUsage usage)
        : width_(width)
        , height_(height)
        , layerCount_(layerCount)
        , mipLevelCount_(mipLevelCount)
        , format_(format)
        , usage_(usage)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: width and height must be positive");
        if (layerCount <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: layerCount must be positive");
        if (mipLevelCount <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: mipLevelCount must be positive");
        const int completeMipLevels = CompleteMipLevelCount(width, height);
        if (mipLevelCount > completeMipLevels)
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: mipLevelCount " +
                std::to_string(mipLevelCount) + " exceeds the complete " +
                std::to_string(completeMipLevels) + "-level chain for " +
                std::to_string(width) + "x" + std::to_string(height));

        const int formatOrdinal = static_cast<int>(format);
        if (formatOrdinal < static_cast<int>(SurfaceFormat::Color) ||
            formatOrdinal > static_cast<int>(SurfaceFormat::UShortEXT))
        {
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: format is not a declared SurfaceFormat");
        }

        const std::uint32_t usageBits = static_cast<std::uint32_t>(usage);
        if ((usageBits & ~AllowedTexture2DArrayUsages) != 0)
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: usage contains an unknown bit");
        if ((usageBits & static_cast<std::uint32_t>(Texture2DArrayUsage::Sampled)) == 0)
            throw std::invalid_argument(
                "CNA::Graphics::Texture2DArrayDescriptor: Sampled usage is required");
    }

    int Texture2DArrayDescriptor::getWidth() const noexcept { return width_; }
    int Texture2DArrayDescriptor::getHeight() const noexcept { return height_; }
    int Texture2DArrayDescriptor::getLayerCount() const noexcept { return layerCount_; }
    int Texture2DArrayDescriptor::getMipLevelCount() const noexcept { return mipLevelCount_; }
    SurfaceFormat Texture2DArrayDescriptor::getFormat() const noexcept { return format_; }
    Texture2DArrayUsage Texture2DArrayDescriptor::getUsage() const noexcept { return usage_; }

    Texture2DArray::Prepared Texture2DArray::prepare(
        GraphicsDevice& device, const Texture2DArrayDescriptor& descriptor)
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
                "CNA::Graphics::Texture2DArray: " +
                std::to_string(descriptor.getWidth()) + "x" +
                std::to_string(descriptor.getHeight()) +
                " exceeds the device's maximum texture dimension of " +
                std::to_string(maxDimension.value));
        }

        const CNA::RendererLimitValue maxLayers =
            device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureArrayLayers);
        if (!maxLayers.known || maxLayers.value == 0)
            ThrowLimitUnavailable(device, "maximum array-layer count");
        if (static_cast<std::uint64_t>(descriptor.getLayerCount()) > maxLayers.value)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: " +
                std::to_string(descriptor.getLayerCount()) +
                " layers exceeds this device's maximum of " +
                std::to_string(maxLayers.value));
        }

        const std::uint32_t requiredUsages = RequiredFormatUsages(descriptor);
        const CNA::RendererFormatSupport formatSupport =
            device.GetRendererSurfaceFormatSupportEXT(descriptor.getFormat());
        if (!formatSupport.Supports(static_cast<CNA::RendererFormatUsage>(requiredUsages)))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: SurfaceFormat " +
                std::to_string(static_cast<int>(descriptor.getFormat())) +
                " does not have every known-and-supported usage required by this descriptor on " +
                std::string(device.GetGraphicsRendererName()));
        }

        std::unique_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer> native =
            device.GetRenderer().CreateTexture2DArrayEXT(
                descriptor.getWidth(), descriptor.getHeight(), descriptor.getLayerCount(),
                descriptor.getMipLevelCount(), static_cast<int>(descriptor.getFormat()),
                static_cast<std::uint32_t>(descriptor.getUsage()));
        if (native == nullptr)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: the '" +
                std::string(device.GetGraphicsRendererName()) +
                "' renderer advertised the descriptor's limits and format usages but did not "
                "create a texture-array resource");
        }

        return Prepared{descriptor,
                        std::shared_ptr<CNA::Internal::Renderers::ITexture2DArrayRenderer>(
                            std::move(native))};
    }

    Texture2DArray::Texture2DArray(
        GraphicsDevice& device, const Texture2DArrayDescriptor& descriptor)
        : Texture2DArray(device, prepare(device, descriptor))
    {
    }

    Texture2DArray::Texture2DArray(GraphicsDevice& device, Prepared prepared)
        : GraphicsResource(&device)
        , descriptor_(std::move(prepared.descriptor))
        , renderer_(std::move(prepared.renderer))
    {
    }

    Texture2DArray::~Texture2DArray() = default;

    const Texture2DArrayDescriptor& Texture2DArray::getDescriptor() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture2DArray");
        return descriptor_;
    }

    void Texture2DArray::setData(
        const int layer, const int mipLevel,
        const Microsoft::Xna::Framework::Rectangle* rectangle, const void* data,
        const std::size_t byteCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture2DArray");
        if ((descriptor_.getUsage() & Texture2DArrayUsage::TransferDestination) ==
            Texture2DArrayUsage::None)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: TransferDestination usage was not declared");
        }
        const TransferRegion region =
            ValidateTransfer(descriptor_, layer, mipLevel, rectangle, data, byteCount);
        if (renderer_ == nullptr ||
            !renderer_->SetData(layer, mipLevel, region.x, region.y, region.width, region.height,
                                data, region.byteCount))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: the renderer refused the complete upload");
        }
    }

    void Texture2DArray::getData(
        const int layer, const int mipLevel,
        const Microsoft::Xna::Framework::Rectangle* rectangle, void* data,
        const std::size_t byteCount) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture2DArray");
        if ((descriptor_.getUsage() & Texture2DArrayUsage::TransferSource) ==
            Texture2DArrayUsage::None)
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: TransferSource usage was not declared");
        }
        const TransferRegion region =
            ValidateTransfer(descriptor_, layer, mipLevel, rectangle, data, byteCount);
        if (renderer_ == nullptr ||
            !renderer_->GetData(layer, mipLevel, region.x, region.y, region.width, region.height,
                                data, region.byteCount))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::Texture2DArray: the renderer refused the complete readback");
        }
    }

    const std::string& Texture2DArray::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.Texture2DArray";
        return name;
    }

    void Texture2DArray::Dispose(const bool disposing)
    {
        if (getIsDisposedProperty())
            return;
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }
}

namespace Microsoft::Xna::Framework::Graphics
{
    void ShaderEffect::SetTextureArrayEXT(
        const int unit, CNA::Graphics::Texture2DArray& texture)
    {
        if (texture.getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture2DArray");
        if (effectRenderer_ == nullptr ||
            !effectRenderer_->BindTexture2DArrayEXT(unit, texture.renderer_))
        {
            throw System::NotSupportedException(
                "ShaderEffect::SetTextureArrayEXT: the active renderer refused texture-array "
                "sampling for unit " + std::to_string(unit));
        }
    }

    void ShaderEffect::ClearTextureArrayEXT(const int unit)
    {
        if (effectRenderer_ == nullptr ||
            !effectRenderer_->BindTexture2DArrayEXT(unit, nullptr))
        {
            throw System::NotSupportedException(
                "ShaderEffect::ClearTextureArrayEXT: the active renderer refused texture-array "
                "sampling for unit " + std::to_string(unit));
        }
    }
}

#endif // CNA_CNAEXT
