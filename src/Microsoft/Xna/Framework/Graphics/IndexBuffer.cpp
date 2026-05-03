#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    IndexBuffer::IndexBuffer(GraphicsDevice& device, int indexCount)
        : backend_(device.GetBackend().CreateIndexBuffer16(indexCount)) {}

    IndexBuffer::~IndexBuffer() = default;

    void IndexBuffer::SetData(const std::uint16_t* indices, int count) {
        backend_->SetData16(indices, count);
    }

    int IndexBuffer::IndexCount() const {
        return backend_->GetIndexCount();
    }
}
