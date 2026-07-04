// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    VertexBuffer::VertexBuffer(GraphicsDevice& device, int vertexCount)
        : VertexBuffer(device, VertexDeclaration{}, vertexCount, BufferUsage::None, false)
    {
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& vertexDeclaration,
                               int vertexCount,
                               BufferUsage bufferUsage)
        : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage, false)
    {
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& vertexDeclaration,
                               int vertexCount,
                               BufferUsage bufferUsage,
                               bool /*dynamic*/)
        : GraphicsResource(&device)
        , backend_(device.GetBackend().CreateVertexBuffer(vertexCount))
        , vertexDeclaration_(vertexDeclaration)
        , bufferUsage_(bufferUsage)
        , vertexCount_(vertexCount)
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

    void VertexBuffer::SetData(const VertexPositionColor* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
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
            packed[i].x = data[i].Position.X;
            packed[i].y = data[i].Position.Y;
            packed[i].z = data[i].Position.Z;
            packed[i].r = data[i].Color.getRProperty();
            packed[i].g = data[i].Color.getGProperty();
            packed[i].b = data[i].Color.getBProperty();
            packed[i].a = data[i].Color.getAProperty();
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionColor* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::SetData(const VertexPositionColorTexture* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
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
            packed[i].x = data[i].Position.X;
            packed[i].y = data[i].Position.Y;
            packed[i].z = data[i].Position.Z;
            packed[i].r = data[i].Color.getRProperty();
            packed[i].g = data[i].Color.getGProperty();
            packed[i].b = data[i].Color.getBProperty();
            packed[i].a = data[i].Color.getAProperty();
            packed[i].u = data[i].TextureCoordinate.X;
            packed[i].v = data[i].TextureCoordinate.Y;
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionColorTexture* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
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
            packed[i].x  = data[i].Position.X;
            packed[i].y  = data[i].Position.Y;
            packed[i].z  = data[i].Position.Z;
            packed[i].nx = data[i].Normal.X;
            packed[i].ny = data[i].Normal.Y;
            packed[i].nz = data[i].Normal.Z;
            packed[i].u  = data[i].TextureCoordinate.X;
            packed[i].v  = data[i].TextureCoordinate.Y;
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::SetData(const VertexPositionTexture* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        // VertexPositionTexture has a virtual base (IVertexType). Pack into
        // GPU layout: float3 position + float2 texcoord = 20 bytes.
        struct GpuVertex {
            float x, y, z;
            float u, v;
        };
        static_assert(sizeof(GpuVertex) == 20, "GpuVertex (VPT) must be 20 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x = data[i].Position.X;
            packed[i].y = data[i].Position.Y;
            packed[i].z = data[i].Position.Z;
            packed[i].u = data[i].TextureCoordinate.X;
            packed[i].v = data[i].TextureCoordinate.Y;
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionTexture* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTextureSkinned* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        // VertexPositionNormalTextureSkinned has a virtual base (IVertexType). Pack into
        // GPU layout: float3 position + float3 normal + float2 texcoord + float4 weights
        // + byte4 indices = 52 bytes.
        struct GpuVertex {
            float x, y, z;
            float nx, ny, nz;
            float u, v;
            float w0, w1, w2, w3;
            std::uint8_t i0, i1, i2, i3;
        };
        static_assert(sizeof(GpuVertex) == 52, "GpuVertex (VPNTSkinned) must be 52 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x  = data[i].Position.X;
            packed[i].y  = data[i].Position.Y;
            packed[i].z  = data[i].Position.Z;
            packed[i].nx = data[i].Normal.X;
            packed[i].ny = data[i].Normal.Y;
            packed[i].nz = data[i].Normal.Z;
            packed[i].u  = data[i].TextureCoordinate.X;
            packed[i].v  = data[i].TextureCoordinate.Y;
            packed[i].w0 = data[i].BlendWeight.X;
            packed[i].w1 = data[i].BlendWeight.Y;
            packed[i].w2 = data[i].BlendWeight.Z;
            packed[i].w3 = data[i].BlendWeight.W;
            packed[i].i0 = data[i].BlendIndices[0];
            packed[i].i1 = data[i].BlendIndices[1];
            packed[i].i2 = data[i].BlendIndices[2];
            packed[i].i3 = data[i].BlendIndices[3];
        }
        backend_->SetData(packed.data(), count, sizeof(GpuVertex));
    }

    void VertexBuffer::SetData(const VertexPositionNormalTextureSkinned* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::SetDataRaw(const void* data, int count, int stride)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        backend_->SetData(data, count, static_cast<std::size_t>(stride));
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionColor* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        struct GpuVertex { float x, y, z; std::uint8_t r, g, b, a; };
        static_assert(sizeof(GpuVertex) == 16);
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            const auto& v = data[startIndex + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        backend_->SetDataWithOptions(packed.data(), elementCount, sizeof(GpuVertex), options);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionColorTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        struct GpuVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
        static_assert(sizeof(GpuVertex) == 24);
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            const auto& v = data[startIndex + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        backend_->SetDataWithOptions(packed.data(), elementCount, sizeof(GpuVertex), options);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionNormalTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        struct GpuVertex { float x, y, z, nx, ny, nz, u, v; };
        static_assert(sizeof(GpuVertex) == 32);
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            const auto& v = data[startIndex + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        backend_->SetDataWithOptions(packed.data(), elementCount, sizeof(GpuVertex), options);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        struct GpuVertex { float x, y, z, u, v; };
        static_assert(sizeof(GpuVertex) == 20);
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            const auto& v = data[startIndex + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        backend_->SetDataWithOptions(packed.data(), elementCount, sizeof(GpuVertex), options);
    }
}
