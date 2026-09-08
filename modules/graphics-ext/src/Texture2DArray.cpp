// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/Texture2DArray.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
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

    Texture2DArray::Prepared Texture2DArray::Prepare(
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
        : Texture2DArray(device, Prepare(device, descriptor))
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

#endif // CNA_CNAEXT
