// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/StorageBuffer.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/NotSupportedException.hpp"

#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    StorageBuffer::StorageBuffer(GraphicsDevice& device, const std::size_t byteSize)
        : byteSize_(byteSize)
    {
        if (byteSize == 0)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer: the size in bytes must be positive");
        if (!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer: the '"
                + std::string(device.GetGraphicsRendererName())
                + "' renderer has no compute support, so it has no storage buffers either");

        renderer_ = device.GetRenderer().CreateStorageBuffer(byteSize);
        if (renderer_ == nullptr)
            throw System::NotSupportedException(
                "CNA::Graphics::StorageBuffer: the '"
                + std::string(device.GetGraphicsRendererName())
                + "' renderer reports compute support but did not create a storage buffer");
    }

    StorageBuffer::~StorageBuffer() = default;

    void StorageBuffer::setBytes(const void* data, const std::size_t byteSize)
    {
        if (data == nullptr)
            throw std::invalid_argument("CNA::Graphics::StorageBuffer::setBytes: data is null");
        if (byteSize > byteSize_)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer::setBytes: more bytes than the buffer holds");
        renderer_->SetData(data, byteSize);
    }

    void StorageBuffer::getBytes(void* out, const std::size_t byteSize) const
    {
        if (out == nullptr)
            throw std::invalid_argument("CNA::Graphics::StorageBuffer::getBytes: out is null");
        if (byteSize > byteSize_)
            throw std::invalid_argument(
                "CNA::Graphics::StorageBuffer::getBytes: more bytes than the buffer holds");
        renderer_->GetData(out, byteSize);
    }

    std::size_t StorageBuffer::getByteSize() const { return byteSize_; }

    CNA::Internal::Renderers::IStorageBufferRenderer* StorageBuffer::getRendererEXT() const
    {
        return renderer_.get();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
