#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    VertexBuffer::VertexBuffer(GraphicsDevice& device, int vertexCount)
        : backend_(device.GetBackend().CreateVertexBuffer(vertexCount)) {}

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& /*vertexDeclaration*/,
                               int vertexCount,
                               BufferUsage /*bufferUsage*/)
        : backend_(device.GetBackend().CreateVertexBuffer(vertexCount)) {}

    VertexBuffer::~VertexBuffer() = default;

    void VertexBuffer::SetData(const VertexPositionColor* vertices, int count) {
        backend_->SetData(vertices, count, sizeof(VertexPositionColor));
    }

    int VertexBuffer::VertexCount() const {
        return backend_->GetVertexCount();
    }
}
