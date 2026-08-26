// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer>
        CreateVertexBufferRenderer(GraphicsDevice& device, int vertexCount)
        {
            if (vertexCount < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "vertexCount", std::to_string(vertexCount),
                    "The vertex count must be non-negative.");
            }
            return device.GetRenderer().CreateVertexBuffer(vertexCount);
        }

        std::size_t CheckedByteCount(int elementCount,
                                     std::size_t elementSize,
                                     const char* parameterName)
        {
            if (elementCount < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(elementCount),
                    "The element count must be non-negative.");
            }
            const auto unsignedCount = static_cast<std::size_t>(elementCount);
            if (elementSize != 0 &&
                unsignedCount > std::numeric_limits<std::size_t>::max() / elementSize)
            {
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(elementCount),
                    "The requested byte range is too large.");
            }
            return unsignedCount * elementSize;
        }

        std::size_t CheckedByteOffset(int startIndex, std::size_t elementSize)
        {
            if (startIndex < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "startIndex", std::to_string(startIndex),
                    "The start index must be non-negative.");
            }
            const auto unsignedStart = static_cast<std::size_t>(startIndex);
            if (elementSize != 0 &&
                unsignedStart > std::numeric_limits<std::size_t>::max() / elementSize)
            {
                throw System::ArgumentOutOfRangeException(
                    "startIndex", std::to_string(startIndex),
                    "The requested byte offset is too large.");
            }
            return unsignedStart * elementSize;
        }

        std::size_t VertexElementSize(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single: return 4;
                case VertexElementFormat::Vector2: return 8;
                case VertexElementFormat::Vector3: return 12;
                case VertexElementFormat::Vector4: return 16;
                case VertexElementFormat::Color:
                case VertexElementFormat::Byte4:
                case VertexElementFormat::Short2:
                case VertexElementFormat::NormalizedShort2:
                case VertexElementFormat::HalfVector2:
                    return 4;
                case VertexElementFormat::Short4:
                case VertexElementFormat::NormalizedShort4:
                case VertexElementFormat::HalfVector4:
                    return 8;
            }
            return 0;
        }
    }

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
        , renderer_(CreateVertexBufferRenderer(device, vertexCount))
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
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }

    GetTypeNameCPP(VertexBuffer, "Microsoft.Xna.Framework.Graphics.VertexBuffer")

    bool VertexBuffer::ValidateSetDataRange(const void* data,
                                            int startIndex,
                                            int elementCount,
                                            std::size_t sourceElementSize,
                                            std::size_t uploadStride,
                                            bool rawUpload) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");

        // Validate the source range and both byte products before the empty return. The pointer
        // itself is intentionally untouched here: a non-negative startIndex on a zero-element
        // raw-pointer range cannot be compared with caller-owned storage and is never evaluated.
        (void) CheckedByteOffset(startIndex, sourceElementSize);
        (void) CheckedByteCount(elementCount, sourceElementSize, "elementCount");
        (void) CheckedByteCount(elementCount, uploadStride, "elementCount");

        if (uploadStride == 0 ||
            uploadStride > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(uploadStride),
                "The vertex stride must be a positive Int32 byte count.");
        }

        const auto& declarationElements = vertexDeclaration_.GetVertexElements();
        if (!declarationElements.empty())
        {
            if (rawUpload &&
                vertexDeclaration_.getVertexStrideProperty() !=
                    static_cast<int>(uploadStride))
            {
                throw System::ArgumentException(
                    "The raw upload stride must match the VertexBuffer's VertexDeclaration.",
                    "stride");
            }

            // Typed CNA vertices are packed into their GPU stream because the C++ object types
            // contain ABI-only vtable/padding bytes. A built-in type's own declaration already
            // describes exactly that stream, but this buffer may carry any declaration the caller
            // chose, so every declared element still has to fit in the bytes actually uploaded.
            for (const VertexElement& element : declarationElements)
            {
                const int offset = element.getOffsetProperty();
                const std::size_t elementSize =
                    VertexElementSize(element.getVertexElementFormatProperty());
                if (offset < 0 || elementSize == 0 ||
                    static_cast<std::size_t>(offset) > uploadStride ||
                    elementSize > uploadStride - static_cast<std::size_t>(offset))
                {
                    throw System::ArgumentException(
                        "The VertexDeclaration contains an element outside the uploaded vertex stride.",
                        "data");
                }
            }
        }

        // Destination byte offset is implicitly zero in CNA's currently exposed overloads.
        // At logical capacity zero that is exactly the logical end, so an empty upload is legal.
        // GFX-025 owns the separate public destination-offset overload work.
        if (elementCount == 0)
            return false;
        if (data == nullptr)
            throw System::ArgumentNullException("data");
        if (elementCount > vertexCount_)
        {
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "The upload exceeds the VertexBuffer's logical capacity.");
        }
        return true;
    }

    void VertexBuffer::UploadValidatedData(const void* data,
                                           int elementCount,
                                           std::size_t uploadStride,
                                           SetDataOptions options,
                                           bool useOptions)
    {
        // GFX-043: propagate this buffer's complete declaration before every real upload. The
        // shared empty branch returns before this method, so even declaration propagation is not
        // a renderer call for an empty operation.
        // CABI-15: content written again, so it is no longer lost. Doing it on this single
        // upload path rather than in the 13 SetData overloads is what keeps the two in step.
        if (auto* const losable = dynamic_cast<CNA::Internal::Graphics::IContentLosable*>(this)) {
            static_cast<DynamicVertexBuffer*>(losable)->ClearContentLostEXT();
        }
        renderer_->SetVertexDeclaration(vertexDeclaration_);
        if (useOptions)
            renderer_->SetDataWithOptions(data, elementCount, uploadStride, options);
        else
            renderer_->SetData(data, elementCount, uploadStride);

        const std::size_t byteCount =
            CheckedByteCount(elementCount, uploadStride, "elementCount");
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        cpuShadow_.assign(bytes, bytes + byteCount);
    }

    void VertexBuffer::SetData(const VertexPositionColor* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionColor* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionColor* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        // The C++ object is not the GPU stream: Color inherits a polymorphic IPackedVector
        // base, so a VertexPositionColor carries a vtable pointer and alignment padding that
        // never reach a renderer. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionColorStream;

        if (!ValidateSetDataRange(data,
                                  startIndex,
                                  elementCount,
                                  sizeof(VertexPositionColor),
                                  sizeof(GpuVertex),
                                  false))
        {
            return;
        }

        const VertexPositionColor* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x = source[i].Position.X;
            packed[i].y = source[i].Position.Y;
            packed[i].z = source[i].Position.Z;
            packed[i].r = source[i].Color.getRProperty();
            packed[i].g = source[i].Color.getGProperty();
            packed[i].b = source[i].Color.getBProperty();
            packed[i].a = source[i].Color.getAProperty();
        }
        UploadValidatedData(
            packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionColor* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionColor* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        using GpuVertex = CNA::Internal::Graphics::PositionColorStream;
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionColor(Vector3(v.x, v.y, v.z), Color(v.r, v.g, v.b, v.a));
        }
    }

    void VertexBuffer::SetData(const VertexPositionColorTexture* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionColorTexture* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionColorTexture* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionColorTextureStream;

        if (!ValidateSetDataRange(data,
                                  startIndex,
                                  elementCount,
                                  sizeof(VertexPositionColorTexture),
                                  sizeof(GpuVertex),
                                  false))
        {
            return;
        }

        const VertexPositionColorTexture* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x = source[i].Position.X;
            packed[i].y = source[i].Position.Y;
            packed[i].z = source[i].Position.Z;
            packed[i].r = source[i].Color.getRProperty();
            packed[i].g = source[i].Color.getGProperty();
            packed[i].b = source[i].Color.getBProperty();
            packed[i].a = source[i].Color.getAProperty();
            packed[i].u = source[i].TextureCoordinate.X;
            packed[i].v = source[i].TextureCoordinate.Y;
        }
        UploadValidatedData(
            packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionColorTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionColorTexture* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        using GpuVertex = CNA::Internal::Graphics::PositionColorTextureStream;
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionColorTexture(Vector3(v.x, v.y, v.z), Color(v.r, v.g, v.b, v.a),
                                                  Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionNormalTexture* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureStream;

        if (!ValidateSetDataRange(data,
                                  startIndex,
                                  elementCount,
                                  sizeof(VertexPositionNormalTexture),
                                  sizeof(GpuVertex),
                                  false))
        {
            return;
        }

        const VertexPositionNormalTexture* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x  = source[i].Position.X;
            packed[i].y  = source[i].Position.Y;
            packed[i].z  = source[i].Position.Z;
            packed[i].nx = source[i].Normal.X;
            packed[i].ny = source[i].Normal.Y;
            packed[i].nz = source[i].Normal.Z;
            packed[i].u  = source[i].TextureCoordinate.X;
            packed[i].v  = source[i].TextureCoordinate.Y;
        }
        UploadValidatedData(
            packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionNormalTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTexture* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureStream;
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionNormalTexture(Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz),
                                                   Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionTexture* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionTexture* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionTexture* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionTextureStream;

        if (!ValidateSetDataRange(data,
                                  startIndex,
                                  elementCount,
                                  sizeof(VertexPositionTexture),
                                  sizeof(GpuVertex),
                                  false))
        {
            return;
        }

        const VertexPositionTexture* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x = source[i].Position.X;
            packed[i].y = source[i].Position.Y;
            packed[i].z = source[i].Position.Z;
            packed[i].u = source[i].TextureCoordinate.X;
            packed[i].v = source[i].TextureCoordinate.Y;
        }
        UploadValidatedData(
            packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionTexture* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        using GpuVertex = CNA::Internal::Graphics::PositionTextureStream;
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionTexture(Vector3(v.x, v.y, v.z), Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTextureSkinned* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTextureSkinned* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(
        const VertexPositionNormalTextureSkinned* data,
        int startIndex,
        int elementCount,
        SetDataOptions options,
        bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureSkinnedStream;

        if (!ValidateSetDataRange(data,
                                  startIndex,
                                  elementCount,
                                  sizeof(VertexPositionNormalTextureSkinned),
                                  sizeof(GpuVertex),
                                  false))
        {
            return;
        }

        const VertexPositionNormalTextureSkinned* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x  = source[i].Position.X;
            packed[i].y  = source[i].Position.Y;
            packed[i].z  = source[i].Position.Z;
            packed[i].nx = source[i].Normal.X;
            packed[i].ny = source[i].Normal.Y;
            packed[i].nz = source[i].Normal.Z;
            packed[i].u  = source[i].TextureCoordinate.X;
            packed[i].v  = source[i].TextureCoordinate.Y;
            packed[i].w0 = source[i].BlendWeight.X;
            packed[i].w1 = source[i].BlendWeight.Y;
            packed[i].w2 = source[i].BlendWeight.Z;
            packed[i].w3 = source[i].BlendWeight.W;
            packed[i].i0 = source[i].BlendIndices[0];
            packed[i].i1 = source[i].BlendIndices[1];
            packed[i].i2 = source[i].BlendIndices[2];
            packed[i].i3 = source[i].BlendIndices[3];
        }
        UploadValidatedData(
            packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionNormalTextureSkinned* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTextureSkinned* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureSkinnedStream;
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionNormalTextureSkinned(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz), Vector2(v.u, v.v),
                Vector4(v.w0, v.w1, v.w2, v.w3),
                std::array<std::uint8_t, 4>{v.i0, v.i1, v.i2, v.i3});
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTexture* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        // VertexPositionNormalTangentTexture has a virtual base (IVertexType). Pack into GPU
        // layout: float3 position + float3 normal + float4 tangent + float2 texcoord = 48 bytes.
        struct GpuVertex {
            float x, y, z;
            float nx, ny, nz;
            float tx, ty, tz, tw;
            float u, v;
        };
        static_assert(sizeof(GpuVertex) == 48, "GpuVertex (VPNTangentT) must be 48 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x  = data[i].Position.X;
            packed[i].y  = data[i].Position.Y;
            packed[i].z  = data[i].Position.Z;
            packed[i].nx = data[i].Normal.X;
            packed[i].ny = data[i].Normal.Y;
            packed[i].nz = data[i].Normal.Z;
            packed[i].tx = data[i].Tangent.X;
            packed[i].ty = data[i].Tangent.Y;
            packed[i].tz = data[i].Tangent.Z;
            packed[i].tw = data[i].Tangent.W;
            packed[i].u  = data[i].TextureCoordinate.X;
            packed[i].v  = data[i].TextureCoordinate.Y;
        }
        UploadValidatedData(packed.data(), count, sizeof(GpuVertex), SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTexture* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTexture* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        struct GpuVertex {
            float x, y, z;
            float nx, ny, nz;
            float tx, ty, tz, tw;
            float u, v;
        };
        static_assert(sizeof(GpuVertex) == 48, "GpuVertex (VPNTangentT) must be 48 bytes");
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionNormalTangentTexture(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz),
                Vector4(v.tx, v.ty, v.tz, v.tw), Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTextureSkinned* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        // VertexPositionNormalTangentTextureSkinned has a virtual base (IVertexType). Pack into
        // GPU layout: float3 position + float3 normal + float4 tangent + float2 texcoord +
        // float4 weights + byte4 indices = 68 bytes.
        struct GpuVertex {
            float x, y, z;
            float nx, ny, nz;
            float tx, ty, tz, tw;
            float u, v;
            float w0, w1, w2, w3;
            std::uint8_t i0, i1, i2, i3;
        };
        static_assert(sizeof(GpuVertex) == 68, "GpuVertex (VPNTangentTSkinned) must be 68 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x  = data[i].Position.X;
            packed[i].y  = data[i].Position.Y;
            packed[i].z  = data[i].Position.Z;
            packed[i].nx = data[i].Normal.X;
            packed[i].ny = data[i].Normal.Y;
            packed[i].nz = data[i].Normal.Z;
            packed[i].tx = data[i].Tangent.X;
            packed[i].ty = data[i].Tangent.Y;
            packed[i].tz = data[i].Tangent.Z;
            packed[i].tw = data[i].Tangent.W;
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
        UploadValidatedData(packed.data(), count, sizeof(GpuVertex), SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTextureSkinned* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTextureSkinned* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTextureSkinned* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        struct GpuVertex {
            float x, y, z;
            float nx, ny, nz;
            float tx, ty, tz, tw;
            float u, v;
            float w0, w1, w2, w3;
            std::uint8_t i0, i1, i2, i3;
        };
        static_assert(sizeof(GpuVertex) == 68, "GpuVertex (VPNTangentTSkinned) must be 68 bytes");
        if ((static_cast<std::size_t>(startIndex) + static_cast<std::size_t>(elementCount)) * sizeof(GpuVertex)
            > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        const auto* packed = reinterpret_cast<const GpuVertex*>(cpuShadow_.data());
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[startIndex + i];
            data[i] = VertexPositionNormalTangentTextureSkinned(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz),
                Vector4(v.tx, v.ty, v.tz, v.tw), Vector2(v.u, v.v),
                Vector4(v.w0, v.w1, v.w2, v.w3),
                std::array<std::uint8_t, 4>{v.i0, v.i1, v.i2, v.i3});
        }
    }

    void VertexBuffer::SetDataRaw(const void* data, int count, int stride)
    {
        const std::size_t uploadStride =
            stride > 0 ? static_cast<std::size_t>(stride) : 0;
        if (!ValidateSetDataRange(
                data, 0, count, uploadStride, uploadStride, true))
        {
            return;
        }
        UploadValidatedData(
            data, count, uploadStride, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataRawAtEXT(const int offsetInBytes, const void* const data,
                                       const int count, const int stride)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (stride <= 0)
            throw System::ArgumentException(
                "The vertex stride must be positive.", "stride");
        if (count < 0)
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count), "This parameter must not be negative.");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");
        const auto uploadStride = static_cast<std::size_t>(stride);
        if (static_cast<std::size_t>(offsetInBytes) % uploadStride != 0)
            throw System::ArgumentException(
                "The destination offset must fall on a vertex boundary.", "offsetInBytes");
        // The declaration, when there is one, is as binding here as it is for a whole-buffer
        // upload: a window written at a different stride would interleave with what is already
        // there rather than replace part of it.
        if (vertexDeclaration_.getVertexStrideProperty() > 0 &&
            static_cast<std::size_t>(vertexDeclaration_.getVertexStrideProperty()) != uploadStride)
        {
            throw System::ArgumentException(
                "The vertex stride does not match this VertexBuffer's VertexDeclaration.",
                "stride");
        }
        if (count == 0)
            return;
        if (data == nullptr)
            throw System::ArgumentNullException("data");

        const std::size_t capacity =
            CheckedByteCount(vertexCount_, uploadStride, "vertexCount");
        const std::size_t windowBytes = CheckedByteCount(count, uploadStride, "count");
        if (windowBytes > capacity - static_cast<std::size_t>(offsetInBytes))
        {
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count),
                "The windowed upload exceeds the VertexBuffer's logical capacity.");
        }

        // The shadow is the only place a window can be composed, because the renderer contract
        // replaces whole-buffer contents. Growing it to the buffer's full capacity is what makes
        // never-written bytes read as zero rather than as whatever a shorter previous upload left.
        if (cpuShadow_.size() < capacity)
            cpuShadow_.resize(capacity, 0U);

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::copy(bytes, bytes + windowBytes,
                  cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes));

        renderer_->SetVertexDeclaration(vertexDeclaration_);
        renderer_->SetData(
            cpuShadow_.data(), static_cast<int>(capacity / uploadStride), uploadStride);
    }

    void VertexBuffer::GetDataRawEXT(const int offsetInBytes, void* const destination,
                                     const int count, const int stride) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("VertexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        if (stride <= 0)
            throw System::ArgumentException(
                "The vertex stride must be positive.", "stride");
        if (count < 0)
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count), "This parameter must not be negative.");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");
        if (count == 0)
            return;
        if (destination == nullptr)
            throw System::ArgumentNullException("destination");

        const std::size_t windowBytes =
            CheckedByteCount(count, static_cast<std::size_t>(stride), "count");
        if (static_cast<std::size_t>(offsetInBytes) > cpuShadow_.size() ||
            windowBytes > cpuShadow_.size() - static_cast<std::size_t>(offsetInBytes))
        {
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count),
                "The requested window is outside the data this VertexBuffer holds.");
        }
        std::copy(cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes),
                  cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes) +
                      static_cast<std::ptrdiff_t>(windowBytes),
                  static_cast<std::uint8_t*>(destination));
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionColor* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionColorTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionNormalTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }
}
