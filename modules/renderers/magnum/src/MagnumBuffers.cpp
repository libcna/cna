// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumBuffers.hpp"

#include <Corrade/Containers/ArrayView.h>

#include <algorithm>

namespace CNA::Internal::Renderers::Magnum
{
    namespace
    {
        using Kind = Mg::GL::DynamicAttribute::Kind;
        using Components = Mg::GL::DynamicAttribute::Components;
        using DataType = Mg::GL::DynamicAttribute::DataType;

        Components ToComponents(int count)
        {
            switch (count)
            {
                case 1:  return Components::One;
                case 2:  return Components::Two;
                case 3:  return Components::Three;
                default: return Components::Four;
            }
        }

        /// Component count, data type and normalization one `VertexElementFormat` ordinal implies.
        struct FormatDescription
        {
            int components = 4;
            DataType dataType = DataType::Float;
            bool normalized = false;
        };

        FormatDescription DescribeFormat(int vertexElementFormat)
        {
            switch (vertexElementFormat)
            {
                case 0:  return {1, DataType::Float, false};          // Single
                case 1:  return {2, DataType::Float, false};          // Vector2
                case 2:  return {3, DataType::Float, false};          // Vector3
                case 3:  return {4, DataType::Float, false};          // Vector4
                case 4:  return {4, DataType::UnsignedByte, true};    // Color
                case 5:  return {4, DataType::UnsignedByte, false};   // Byte4
                case 6:  return {2, DataType::Short, false};          // Short2
                case 7:  return {4, DataType::Short, false};          // Short4
                case 8:  return {2, DataType::Short, true};           // NormalizedShort2
                case 9:  return {4, DataType::Short, true};           // NormalizedShort4
                case 10: return {2, DataType::Half, false};           // HalfVector2
                case 11: return {4, DataType::Half, false};           // HalfVector4
                default: return {4, DataType::Float, false};
            }
        }

        /**
         * @brief Hands out a process-unique identity for a buffer.
         *
         * Monotonic rather than address-derived: a destroyed buffer's address can be reused, and a
         * cache key built on the address alone would then match a different buffer.
         */
        std::uint64_t NextBufferIdentity()
        {
            static std::uint64_t next = 1;
            return next++;
        }

        /// Vertex arrays one buffer keeps before the oldest is evicted.
        constexpr std::size_t kMeshCacheCapacity = 8;

        MagnumVertexAttribute MakeAttribute(int location, int offset, int components,
                                            bool normalized, int format, bool integral = false)
        {
            MagnumVertexAttribute attribute;
            attribute.location = location;
            attribute.offsetInStream = offset;
            attribute.components = components;
            attribute.normalized = normalized;
            attribute.format = format;
            attribute.integral = integral;
            return attribute;
        }
    }

    Mg::GL::DynamicAttribute ToDynamicAttribute(const MagnumVertexAttribute& attribute)
    {
        const FormatDescription description = DescribeFormat(attribute.format);
        const Kind kind = attribute.integral
            ? Kind::Integral
            : (description.normalized ? Kind::GenericNormalized : Kind::Generic);
        return Mg::GL::DynamicAttribute{
            kind, static_cast<Mg::UnsignedInt>(attribute.location),
            ToComponents(attribute.components), description.dataType};
    }

    std::vector<MagnumVertexAttribute> StockAttributesForStride(std::size_t strideInBytes)
    {
        switch (strideInBytes)
        {
            case 16:  // VertexPositionColor: float3 position + Color
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 4, true,  4)};
            case 20:  // VertexPositionTexture: float3 position + float2 texture coordinate
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 2, false, 1)};
            case 24:  // VertexPositionColorTexture: float3 position + Color + float2 coordinate
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 4, true,  4),
                        MakeAttribute(2, 16, 2, false, 1)};
            case 32:  // VertexPositionNormalTexture: float3 position + float3 normal + float2 uv
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 3, false, 2),
                        MakeAttribute(2, 24, 2, false, 1)};
            // VertexPositionNormalTextureSkinned: the stride-32 layout plus a float4 blend weight
            // and a Byte4 blend index. Stride 56 is the same layout with a per-vertex Color
            // APPENDED at the end rather than inserted, which is what keeps locations 0..4
            // byte-identical between the two and lets one skinned program serve both -- for a
            // stride-52 draw location 5 is simply left unbound.
            case 52:
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 3, false, 2),
                        MakeAttribute(2, 24, 2, false, 1),
                        MakeAttribute(3, 32, 4, false, 3),
                        MakeAttribute(4, 48, 4, false, 5, true)};
            case 56:
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 3, false, 2),
                        MakeAttribute(2, 24, 2, false, 1),
                        MakeAttribute(3, 32, 4, false, 3),
                        MakeAttribute(4, 48, 4, false, 5, true),
                        MakeAttribute(5, 52, 4, true,  4)};
            // VertexPositionNormalTangentTexture: the tangent's w carries the bitangent handedness,
            // which is why it is a float4 rather than a float3. Stride 68 appends the same skinning
            // suffix strides 52/56 use, so locations 0..3 stay byte-identical between the two PBR
            // layouts for exactly the reason 52 and 56 share theirs.
            case 48:
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 3, false, 2),
                        MakeAttribute(2, 24, 4, false, 3),
                        MakeAttribute(3, 40, 2, false, 1)};
            // plan_gltf.md GLTF-462: stride 60 is the rigid PBR record with a second UV set at 48
            // and a packed COLOR_0 at 56; its first four fields are byte-identical to stride 48.
            // Without this row it fell to the empty default below and the mesh had no attributes at
            // all. The two trailing slots stay unlisted because this renderer's PBR shader samples
            // one UV set and reads no colour attribute -- GLTF-465 owns consuming them.
            case 60:
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 3, false, 2),
                        MakeAttribute(2, 24, 4, false, 3),
                        MakeAttribute(3, 40, 2, false, 1)};
            case 68:
                return {MakeAttribute(0,  0, 3, false, 2),
                        MakeAttribute(1, 12, 3, false, 2),
                        MakeAttribute(2, 24, 4, false, 3),
                        MakeAttribute(3, 40, 2, false, 1),
                        MakeAttribute(4, 48, 4, false, 3),
                        MakeAttribute(5, 64, 4, false, 5, true)};
            default:
                return {};
        }
    }

    std::vector<MagnumVertexAttribute> AttributesForDeclaration(
        const std::vector<VertexElement>& elements, int baseLocation, int byteBase)
    {
        std::vector<MagnumVertexAttribute> attributes;
        attributes.reserve(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            const VertexElement& element = elements[i];
            const int format = static_cast<int>(element.getVertexElementFormatProperty());
            const FormatDescription description = DescribeFormat(format);
            attributes.push_back(MakeAttribute(
                baseLocation + static_cast<int>(i),
                element.getOffsetProperty() - byteBase,
                description.components, description.normalized, format));
        }
        return attributes;
    }

    // ---- MagnumVertexBufferRenderer ----

    MagnumVertexBufferRenderer::MagnumVertexBufferRenderer(int vertexCapacity)
        : buffer_(std::make_unique<Mg::GL::Buffer>(Mg::GL::Buffer::TargetHint::Array))
        , identity_(NextBufferIdentity())
        , vertexCapacity_(std::max(0, vertexCapacity))
    {
    }

    Mg::GL::Mesh* MagnumVertexBufferRenderer::FindCachedMesh(
        const std::vector<std::uint64_t>& key) const
    {
        for (const MagnumMeshCacheEntry& entry : meshCache_)
        {
            if (entry.key == key)
                return entry.mesh.get();
        }
        return nullptr;
    }

    Mg::GL::Mesh& MagnumVertexBufferRenderer::StoreCachedMesh(
        std::vector<std::uint64_t> key, std::unique_ptr<Mg::GL::Mesh> mesh) const
    {
        if (meshCache_.size() >= kMeshCacheCapacity)
            meshCache_.erase(meshCache_.begin());
        meshCache_.push_back(MagnumMeshCacheEntry{std::move(key), std::move(mesh)});
        return *meshCache_.back().mesh;
    }

    void MagnumVertexBufferRenderer::SetData(const void* data, int vertexCount,
                                            std::size_t strideInBytes)
    {
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions::None);
    }

    void MagnumVertexBufferRenderer::SetDataWithOptions(const void* data, int vertexCount,
                                                       std::size_t strideInBytes,
                                                       SetDataOptions options)
    {
        if (vertexCount < 0 || strideInBytes == 0)
            return;

        strideInBytes_ = strideInBytes;
        vertexCount_ = vertexCount;

        const std::size_t byteCount = static_cast<std::size_t>(vertexCount) * strideInBytes;
        if (byteCount == 0)
        {
            // An empty upload still has to leave a usable allocation behind: XNA lets a game
            // declare a buffer, upload nothing, and draw zero primitives from it.
            if (allocatedBytes_ == 0 && vertexCapacity_ > 0)
            {
                allocatedBytes_ = static_cast<std::size_t>(vertexCapacity_) * strideInBytes;
                buffer_->setData(
                    Corrade::Containers::ArrayView<const void>{nullptr, allocatedBytes_},
                    Mg::GL::BufferUsage::DynamicDraw);
            }
            return;
        }

        Upload(data, byteCount, options);
    }

    void MagnumVertexBufferRenderer::Upload(const void* data, std::size_t byteCount,
                                           SetDataOptions options)
    {
        const std::size_t capacityBytes =
            std::max(byteCount, static_cast<std::size_t>(vertexCapacity_) * strideInBytes_);

        // SetDataOptions::Discard is XNA's "I am done with the old contents" hint, which maps onto
        // GL buffer orphaning: a fresh allocation lets the driver hand back new storage instead of
        // stalling until draws still reading the old contents retire. Every other option writes in
        // place, which is what NoOverwrite promises and what None conservatively allows.
        const bool reallocate = allocatedBytes_ < capacityBytes || options == SetDataOptions::Discard;
        if (reallocate)
        {
            allocatedBytes_ = capacityBytes;
            buffer_->setData(
                Corrade::Containers::ArrayView<const void>{nullptr, allocatedBytes_},
                Mg::GL::BufferUsage::DynamicDraw);
        }
        buffer_->setSubData(0, Corrade::Containers::ArrayView<const void>{data, byteCount});
    }

    void MagnumVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declarationElements_ = vertexDeclaration.GetVertexElements();
        // Every cached vertex array was built against the previous declaration, so the revision is
        // what keeps a later draw from matching one of them.
        ++declarationRevision_;
        if (strideInBytes_ == 0 && vertexDeclaration.getVertexStrideProperty() > 0)
            strideInBytes_ = static_cast<std::size_t>(vertexDeclaration.getVertexStrideProperty());
    }

    // ---- MagnumIndexBufferRenderer ----

    MagnumIndexBufferRenderer::MagnumIndexBufferRenderer(int indexCapacity, bool thirtyTwoBit)
        : buffer_(std::make_unique<Mg::GL::Buffer>(Mg::GL::Buffer::TargetHint::ElementArray))
        , identity_(NextBufferIdentity())
        , indexCapacity_(std::max(0, indexCapacity))
        , thirtyTwoBit_(thirtyTwoBit)
    {
    }

    void MagnumIndexBufferRenderer::SetData16(const void* data, int indexCount)
    {
        SetData16WithOptions(data, indexCount, SetDataOptions::None);
    }

    void MagnumIndexBufferRenderer::SetData32(const void* data, int indexCount)
    {
        SetData32WithOptions(data, indexCount, SetDataOptions::None);
    }

    void MagnumIndexBufferRenderer::SetData16WithOptions(const void* data, int indexCount,
                                                        SetDataOptions options)
    {
        Upload(data, indexCount, 2, options);
    }

    void MagnumIndexBufferRenderer::SetData32WithOptions(const void* data, int indexCount,
                                                        SetDataOptions options)
    {
        Upload(data, indexCount, 4, options);
    }

    void MagnumIndexBufferRenderer::Upload(const void* data, int indexCount, int indexSize,
                                          SetDataOptions options)
    {
        if (indexCount < 0)
            return;

        indexCount_ = indexCount;
        const std::size_t byteCount = static_cast<std::size_t>(indexCount) * indexSize;
        const std::size_t capacityBytes =
            std::max(byteCount, static_cast<std::size_t>(indexCapacity_) * indexSize);

        if (capacityBytes == 0)
            return;

        // See MagnumVertexBufferRenderer::Upload for why Discard reallocates rather than writing in
        // place.
        if (allocatedBytes_ < capacityBytes || options == SetDataOptions::Discard)
        {
            allocatedBytes_ = capacityBytes;
            buffer_->setData(
                Corrade::Containers::ArrayView<const void>{nullptr, allocatedBytes_},
                Mg::GL::BufferUsage::DynamicDraw);
        }
        if (data != nullptr && byteCount > 0)
            buffer_->setSubData(0, Corrade::Containers::ArrayView<const void>{data, byteCount});
    }

    // ---- MagnumOcclusionQueryRenderer ----

    MagnumOcclusionQueryRenderer::MagnumOcclusionQueryRenderer()
        : query_(std::make_unique<Mg::GL::SampleQuery>(Mg::GL::SampleQuery::Target::SamplesPassed))
    {
    }

    void MagnumOcclusionQueryRenderer::Begin()
    {
        query_->begin();
        started_ = true;
    }

    void MagnumOcclusionQueryRenderer::End()
    {
        if (!started_)
            return;
        query_->end();
    }

    bool MagnumOcclusionQueryRenderer::IsComplete() const
    {
        return started_ && query_->resultAvailable();
    }

    int MagnumOcclusionQueryRenderer::PixelCount() const
    {
        if (!started_)
            return 0;
        return static_cast<int>(query_->result<Mg::UnsignedInt>());
    }
}
