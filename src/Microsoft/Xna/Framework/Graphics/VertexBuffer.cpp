// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    VertexBuffer::VertexBuffer(GraphicsDevice& device, int vertexCount)
        : GraphicsResource(&device)
        , backend_(device.GetBackend().CreateVertexBuffer(vertexCount))
    {
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& /*vertexDeclaration*/,
                               int vertexCount,
                               BufferUsage /*bufferUsage*/)
        : GraphicsResource(&device)
        , backend_(device.GetBackend().CreateVertexBuffer(vertexCount))
    {
    }

    VertexBuffer::~VertexBuffer() = default;
    VertexBuffer::VertexBuffer(VertexBuffer&&) noexcept = default;
    VertexBuffer& VertexBuffer::operator=(VertexBuffer&&) noexcept = default;

    void VertexBuffer::Dispose(bool disposing)
    {
        backend_.reset();
        GraphicsResource::Dispose(disposing);
    }

    GetTypeNameCPP(VertexBuffer, "Microsoft.Xna.Framework.Graphics.VertexBuffer")

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

    void VertexBuffer::SetData(const VertexPositionColorTexture* vertices, int count)
    {
        // VertexPositionColorTexture has a virtual base (IVertexType) so its sizeof
        // includes a vtable pointer. Pack into the GPU layout: float3 + ubyte4 + float2 = 24 bytes.
        struct GpuVertex {
            float x, y, z;
            std::uint8_t r, g, b, a;
            float u, v;
        };
        static_assert(sizeof(GpuVertex) == 24, "GpuVertex (VPC+T) must be 24 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x = vertices[i].Position.X;
            packed[i].y = vertices[i].Position.Y;
            packed[i].z = vertices[i].Position.Z;
            packed[i].r = vertices[i].Color.getRProperty();
            packed[i].g = vertices[i].Color.getGProperty();
            packed[i].b = vertices[i].Color.getBProperty();
            packed[i].a = vertices[i].Color.getAProperty();
            packed[i].u = vertices[i].TextureCoordinate.X;
            packed[i].v = vertices[i].TextureCoordinate.Y;
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* vertices, int count)
    {
        // VertexPositionNormalTexture has a virtual base (IVertexType). Pack into
        // GPU layout: float3 position + float3 normal + float2 texcoord = 32 bytes.
        struct GpuVertex {
            float x, y, z;
            float nx, ny, nz;
            float u, v;
        };
        static_assert(sizeof(GpuVertex) == 32, "GpuVertex (VPNT) must be 32 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x  = vertices[i].Position.X;
            packed[i].y  = vertices[i].Position.Y;
            packed[i].z  = vertices[i].Position.Z;
            packed[i].nx = vertices[i].Normal.X;
            packed[i].ny = vertices[i].Normal.Y;
            packed[i].nz = vertices[i].Normal.Z;
            packed[i].u  = vertices[i].TextureCoordinate.X;
            packed[i].v  = vertices[i].TextureCoordinate.Y;
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionTexture* vertices, int count)
    {
        // VertexPositionTexture has a virtual base (IVertexType). Pack into
        // GPU layout: float3 position + float2 texcoord = 20 bytes.
        struct GpuVertex {
            float x, y, z;
            float u, v;
        };
        static_assert(sizeof(GpuVertex) == 20, "GpuVertex (VPT) must be 20 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x = vertices[i].Position.X;
            packed[i].y = vertices[i].Position.Y;
            packed[i].z = vertices[i].Position.Z;
            packed[i].u = vertices[i].TextureCoordinate.X;
            packed[i].v = vertices[i].TextureCoordinate.Y;
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    int VertexBuffer::getVertexCountProperty() const
    {
        return backend_->GetVertexCount();
    }

    void VertexBuffer::SetDataRaw(const void* data, int count, int stride)
    {
        backend_->SetData(data, count, static_cast<std::size_t>(stride));
    }
}
