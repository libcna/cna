// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/StorageBuffer.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    namespace
    {
        constexpr std::uint32_t AllowedStorageBufferUsages =
            static_cast<std::uint32_t>(StorageBufferUsage::Storage) |
            static_cast<std::uint32_t>(StorageBufferUsage::TransferSource) |
            static_cast<std::uint32_t>(StorageBufferUsage::TransferDestination) |
            static_cast<std::uint32_t>(StorageBufferUsage::IndirectArguments) |
            static_cast<std::uint32_t>(StorageBufferUsage::Vertex) |
            static_cast<std::uint32_t>(StorageBufferUsage::Index) |
            static_cast<std::uint32_t>(StorageBufferUsage::Constant);

        constexpr std::uint32_t AllowedStorageBufferCpuAccess =
            static_cast<std::uint32_t>(StorageBufferCpuAccess::Read) |
            static_cast<std::uint32_t>(StorageBufferCpuAccess::Write);

        constexpr StorageBufferUsage LegacyUsage =
            StorageBufferUsage::Storage |
            StorageBufferUsage::TransferSource |
            StorageBufferUsage::TransferDestination |
            StorageBufferUsage::IndirectArguments;

        constexpr StorageBufferCpuAccess LegacyCpuAccess =
            StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write;

        void ValidateRange(
            const std::size_t capacity, const std::size_t byteOffset,
            const std::size_t byteSize, const char* route)
        {
            if (byteOffset > capacity || byteSize > capacity - byteOffset)
                throw std::invalid_argument(
                    std::string("CNA::Graphics::StorageBuffer::") + route +
                    ": byte range exceeds the buffer");
        }

        [[nodiscard]] bool HasUsage(
            const StorageBufferUsage mask, const StorageBufferUsage required) noexcept
        {
            return (mask & required) == required;
        }

        [[nodiscard]] bool HasCpuAccess(
            const StorageBufferCpuAccess mask,
            const StorageBufferCpuAccess required) noexcept
        {
            return (mask & required) == required;
        }

        void RequireCompute(const GraphicsDevice& device)
        {
            if (device.getIsDisposedProperty())
                throw System::ObjectDisposedException("GraphicsDevice");
            if (!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: the '" +
                    std::string(device.GetGraphicsRendererName()) +
                    "' renderer has no compute support, so it has no storage buffers either");
        }

        void RequireDescriptorCapabilities(
            const GraphicsDevice& device, const StorageBufferDescriptor& descriptor)
        {
            if (device.getIsDisposedProperty())
                throw System::ObjectDisposedException("GraphicsDevice");

            const StorageBufferUsage usage = descriptor.getUsage();
            if (HasUsage(usage, StorageBufferUsage::Storage) &&
                !device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
            {
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: the '" +
                    std::string(device.GetGraphicsRendererName()) +
                    "' renderer has no compute support required by Storage usage");
            }
            if (HasUsage(usage, StorageBufferUsage::IndirectArguments) &&
                !device.SupportsCapability(CNA::GraphicsCapability::IndirectDraw))
            {
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: the '" +
                    std::string(device.GetGraphicsRendererName()) +
                    "' renderer has no indirect-draw support required by IndirectArguments "
                    "usage");
            }
        }
    }

    StorageBufferDescriptor::StorageBufferDescriptor(
        const std::size_t byteSize, const StorageBufferUsage usage,
        const StorageBufferCpuAccess cpuAccess)
        : byteSize_(byteSize)
        , usage_(usage)
        , cpuAccess_(cpuAccess)
    {
        if (byteSize == 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBufferDescriptor: byteSize must be positive");
        const std::uint32_t usageBits = static_cast<std::uint32_t>(usage);
        if (usageBits == 0 || (usageBits & ~AllowedStorageBufferUsages) != 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBufferDescriptor: usage must contain only declared "
                "non-zero bits");
        const std::uint32_t cpuAccessBits = static_cast<std::uint32_t>(cpuAccess);
        if ((cpuAccessBits & ~AllowedStorageBufferCpuAccess) != 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBufferDescriptor: cpuAccess contains an unknown bit");
    }

    std::size_t StorageBufferDescriptor::getByteSize() const noexcept { return byteSize_; }
    StorageBufferUsage StorageBufferDescriptor::getUsage() const noexcept { return usage_; }
    StorageBufferCpuAccess StorageBufferDescriptor::getCpuAccess() const noexcept
    {
        return cpuAccess_;
    }

    StorageBuffer::Prepared StorageBuffer::prepareLegacy(
        GraphicsDevice& device, const std::size_t byteSize)
    {
        const StorageBufferDescriptor descriptor(byteSize, LegacyUsage, LegacyCpuAccess);
        RequireCompute(device);
        auto native = device.GetRenderer().CreateStorageBuffer(byteSize);
        if (native == nullptr)
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer: the '" +
                std::string(device.GetGraphicsRendererName()) +
                "' renderer reports compute support but did not create a storage buffer");
        return Prepared{
            descriptor,
            std::shared_ptr<CNA::Internal::Renderers::IStorageBufferRenderer>(
                std::move(native))};
    }

    StorageBuffer::Prepared StorageBuffer::prepare(
        GraphicsDevice& device, const StorageBufferDescriptor& descriptor)
    {
        RequireDescriptorCapabilities(device, descriptor);
        if (HasUsage(descriptor.getUsage(), StorageBufferUsage::Storage))
        {
            const CNA::RendererLimitValue maximum =
                device.GetRendererLimitEXT(CNA::RendererLimit::MaxStorageBufferBytes);
            if (!maximum.known || maximum.value == 0)
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: the '" +
                    std::string(device.GetGraphicsRendererName()) +
                    "' renderer exposes no implemented maximum storage-buffer size");
            if (static_cast<std::uint64_t>(descriptor.getByteSize()) > maximum.value)
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: byteSize " +
                    std::to_string(descriptor.getByteSize()) +
                    " exceeds the device maximum of " + std::to_string(maximum.value));
        }
        if (HasUsage(descriptor.getUsage(), StorageBufferUsage::Constant))
        {
            const CNA::RendererLimitValue maximum =
                device.GetRendererLimitEXT(CNA::RendererLimit::MaxUniformBufferBytes);
            if (!maximum.known || maximum.value == 0)
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: the '" +
                    std::string(device.GetGraphicsRendererName()) +
                    "' renderer exposes no implemented maximum constant-buffer size");
            if (static_cast<std::uint64_t>(descriptor.getByteSize()) > maximum.value)
                throw System::NotSupportedException(
                    "CNA::Graphics::StorageBuffer: constant-buffer byteSize " +
                    std::to_string(descriptor.getByteSize()) +
                    " exceeds the device maximum of " + std::to_string(maximum.value));
        }

        auto native = device.GetRenderer().CreateStorageBufferEXT(
            descriptor.getByteSize(),
            static_cast<std::uint32_t>(descriptor.getUsage()),
            static_cast<std::uint32_t>(descriptor.getCpuAccess()));
        if (native == nullptr)
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer: the '" +
                std::string(device.GetGraphicsRendererName()) +
                "' renderer refused the requested usage and CPU-access descriptor");
        return Prepared{
            descriptor,
            std::shared_ptr<CNA::Internal::Renderers::IStorageBufferRenderer>(
                std::move(native))};
    }

    StorageBuffer::StorageBuffer(GraphicsDevice& device, const std::size_t byteSize)
        : StorageBuffer(device, prepareLegacy(device, byteSize))
    {
    }

    StorageBuffer::StorageBuffer(
        GraphicsDevice& device, const StorageBufferDescriptor& descriptor)
        : StorageBuffer(device, prepare(device, descriptor))
    {
    }

    StorageBuffer::StorageBuffer(GraphicsDevice& device, Prepared prepared)
        : GraphicsResource(&device)
        , descriptor_(std::move(prepared.descriptor))
        , renderer_(std::move(prepared.renderer))
    {
    }

    StorageBuffer::~StorageBuffer() = default;

    void StorageBuffer::setBytes(const void* data, const std::size_t byteSize)
    {
        setBytes(0, data, byteSize);
    }

    void StorageBuffer::setBytes(
        const std::size_t byteOffset, const void* data, const std::size_t byteSize)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageBuffer");
        if (!HasCpuAccess(descriptor_.getCpuAccess(), StorageBufferCpuAccess::Write))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::setBytes: CPU Write access was not declared");
        if (data == nullptr && byteSize != 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer::setBytes: data is null");
        ValidateRange(descriptor_.getByteSize(), byteOffset, byteSize, "setBytes");
        if (renderer_ == nullptr || !renderer_->SetDataRangeEXT(byteOffset, data, byteSize))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::setBytes: renderer refused the complete range");
    }

    void StorageBuffer::getBytes(void* out, const std::size_t byteSize) const
    {
        getBytes(0, out, byteSize);
    }

    void StorageBuffer::getBytes(
        const std::size_t byteOffset, void* out, const std::size_t byteSize) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageBuffer");
        if (!HasCpuAccess(descriptor_.getCpuAccess(), StorageBufferCpuAccess::Read))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::getBytes: CPU Read access was not declared");
        if (out == nullptr && byteSize != 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer::getBytes: out is null");
        ValidateRange(descriptor_.getByteSize(), byteOffset, byteSize, "getBytes");
        if (renderer_ == nullptr || !renderer_->GetDataRangeEXT(byteOffset, out, byteSize))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::getBytes: renderer refused the complete range");
    }

    void StorageBuffer::copyTo(
        StorageBuffer& destination, const std::size_t sourceByteOffset,
        const std::size_t destinationByteOffset, const std::size_t byteSize) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageBuffer");
        if (destination.getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageBuffer");
        if (getGraphicsDeviceProperty() != destination.getGraphicsDeviceProperty())
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer::copyTo: buffers belong to different devices");
        if (!HasUsage(descriptor_.getUsage(), StorageBufferUsage::TransferSource))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::copyTo: source has no TransferSource usage");
        if (!HasUsage(destination.descriptor_.getUsage(),
                      StorageBufferUsage::TransferDestination))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::copyTo: destination has no "
                "TransferDestination usage");
        ValidateRange(descriptor_.getByteSize(), sourceByteOffset, byteSize, "copyTo source");
        ValidateRange(destination.descriptor_.getByteSize(), destinationByteOffset,
                      byteSize, "copyTo destination");
        if (this == &destination && byteSize != 0 &&
            sourceByteOffset < destinationByteOffset + byteSize &&
            destinationByteOffset < sourceByteOffset + byteSize)
        {
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer::copyTo: same-buffer ranges overlap");
        }
        if (renderer_ == nullptr || destination.renderer_ == nullptr ||
            !renderer_->CopyToEXT(*destination.renderer_, sourceByteOffset,
                                  destinationByteOffset, byteSize))
        {
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer::copyTo: renderer refused the complete copy");
        }
    }

    const StorageBufferDescriptor& StorageBuffer::getDescriptor() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageBuffer");
        return descriptor_;
    }

    std::size_t StorageBuffer::getByteSize() const
    {
        return getDescriptor().getByteSize();
    }

    CNA::Internal::Renderers::IStorageBufferRenderer* StorageBuffer::getRendererEXT() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("StorageBuffer");
        return renderer_.get();
    }

    const std::string& StorageBuffer::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.StorageBuffer";
        return name;
    }

    void StorageBuffer::Dispose(const bool disposing)
    {
        if (getIsDisposedProperty()) return;
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
