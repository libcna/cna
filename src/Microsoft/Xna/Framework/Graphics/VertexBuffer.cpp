#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    VertexBuffer::VertexBuffer(GraphicsDevice& device, int vertexCount)
        : backend_(device.GetBackend().CreateVertexBuffer(vertexCount))
    {
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& /*vertexDeclaration*/,
                               int vertexCount,
                               BufferUsage /*bufferUsage*/)
        : backend_(device.GetBackend().CreateVertexBuffer(vertexCount))
    {
    }

    VertexBuffer::~VertexBuffer() = default;

    void VertexBuffer::SetData(const VertexPositionColor* vertices, int count)
    {
        // VertexPositionColor contains a Color member that inherits from a virtual
        // base class (IPackedVector), so sizeof(Color) == 16 (vtable + data + padding)
        // and sizeof(VertexPositionColor) == 32, not 16. The EasyGL 3D shader pipeline
        // expects a compact 16-byte layout: vec3 position + 4 ubytes RGBA. We pack
        // the vertex data into that compact form here before uploading to the GPU.
        struct GpuVertex {
            float x, y, z;
            std::uint8_t r, g, b, a;
        };
        static_assert(sizeof(GpuVertex) == 16, "GpuVertex must be exactly 16 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x = vertices[i].Position.X;
            packed[i].y = vertices[i].Position.Y;
            packed[i].z = vertices[i].Position.Z;
            packed[i].r = vertices[i].Color.getRProperty();
            packed[i].g = vertices[i].Color.getGProperty();
            packed[i].b = vertices[i].Color.getBProperty();
            packed[i].a = vertices[i].Color.getAProperty();
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    int VertexBuffer::VertexCount() const
    {
        return backend_->GetVertexCount();
    }
}
