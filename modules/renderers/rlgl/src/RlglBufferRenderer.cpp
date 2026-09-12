// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"

#include "RlglBridge.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SetDataOptions;
        using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;

        [[nodiscard]] int CheckedByteCapacity(
            const int elementCapacity, const std::size_t elementSize, const char* label)
        {
            if (elementCapacity < 0)
                throw std::invalid_argument(std::string("RLGL: ") + label +
                    " capacity must be non-negative");
            if (elementSize == 0 ||
                static_cast<std::size_t>(elementCapacity) >
                    static_cast<std::size_t>(std::numeric_limits<int>::max()) / elementSize)
            {
                throw std::overflow_error(std::string("RLGL: ") + label +
                    " byte capacity exceeds rlgl's Int32 buffer API");
            }
            return static_cast<int>(static_cast<std::size_t>(elementCapacity) * elementSize);
        }

        [[nodiscard]] int CheckedUploadBytes(
            const int count, const int capacity, const std::size_t elementSize,
            const void* const data, const char* label)
        {
            if (count < 0 || count > capacity)
                throw std::out_of_range(std::string("RLGL: ") + label +
                    " upload exceeds its logical capacity");
            if (count == 0) return 0;
            if (data == nullptr)
                throw std::invalid_argument(std::string("RLGL: ") + label +
                    " upload data must not be null");
            return CheckedByteCapacity(count, elementSize, label);
        }

        class RlglVertexBufferRenderer final : public IVertexBufferRenderer
        {
        public:
            explicit RlglVertexBufferRenderer(const int vertexCapacity)
                : capacity_(vertexCapacity)
            {
                if (capacity_ < 0)
                    throw std::invalid_argument(
                        "RLGL: vertex-buffer capacity must be non-negative");
            }

            ~RlglVertexBufferRenderer() override
            {
                Bridge::DestroyBuffer(id_);
            }

            void SetData(
                const void* const data, const int vertexCount,
                const std::size_t stride) override
            {
                Upload(data, vertexCount, stride, SetDataOptions::None);
            }

            void SetDataWithOptions(
                const void* const data, const int vertexCount,
                const std::size_t stride, const SetDataOptions options) override
            {
                Upload(data, vertexCount, stride, options);
            }

            void SetVertexDeclaration(const VertexDeclaration& declaration) override
            {
                declaration_ = declaration.GetVertexElements();
                const int declaredStride = declaration.getVertexStrideProperty();
                if (declaredStride > 0)
                    EnsureStorage(static_cast<std::size_t>(declaredStride));
            }

            [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
            [[nodiscard]] unsigned int NativeId() const noexcept { return id_; }
            [[nodiscard]] int Capacity() const noexcept { return capacity_; }
            [[nodiscard]] std::size_t Stride() const noexcept { return stride_; }
            [[nodiscard]] const std::vector<VertexElement>& Declaration() const noexcept
            {
                return declaration_;
            }

            [[nodiscard]] BufferResourceSnapshot Snapshot() const
            {
                BufferResourceSnapshot result;
                result.id = id_;
                result.capacity = capacity_;
                result.count = vertexCount_;
                result.stride = stride_;
                result.cpuBytes = cpuBytes_;
                result.declaration = declaration_;
                if (id_ != 0)
                {
                    const Bridge::BufferSnapshot native =
                        Bridge::GetBufferSnapshotForTesting(id_, false);
                    result.nativeByteSize = native.byteSize;
                    result.nativeUsage = native.usage;
                    result.nativeBytes = native.bytes;
                }
                result.ordinaryUploadCount = ordinaryUploadCount_;
                result.discardUploadCount = discardUploadCount_;
                result.noOverwriteUploadCount = noOverwriteUploadCount_;
                return result;
            }

        private:
            void EnsureStorage(const std::size_t stride)
            {
                if (stride == 0)
                    throw std::invalid_argument("RLGL: vertex stride must be positive");
                if (!declaration_.empty() && stride_ != 0 && stride_ != stride)
                    throw std::invalid_argument(
                        "RLGL: vertex stride changed after declaration allocation");
                if (id_ != 0 && stride_ == stride) return;

                const int byteCapacity =
                    CheckedByteCapacity(capacity_, stride, "vertex-buffer");
                Bridge::DestroyBuffer(id_);
                id_ = Bridge::CreateVertexBuffer(byteCapacity);
                stride_ = stride;
                cpuBytes_.assign(static_cast<std::size_t>(byteCapacity), 0u);
            }

            void Upload(
                const void* const data, const int vertexCount,
                const std::size_t stride, const SetDataOptions options)
            {
                if (!declaration_.empty())
                {
                    if (stride_ != 0 && stride != stride_)
                    {
                        throw std::invalid_argument(
                            "RLGL: upload stride does not match the vertex declaration");
                    }
                }

                const int byteCount = CheckedUploadBytes(
                    vertexCount, capacity_, stride, data, "vertex-buffer");
                if (byteCount == 0) return;
                if (options != SetDataOptions::None &&
                    options != SetDataOptions::Discard &&
                    options != SetDataOptions::NoOverwrite)
                {
                    throw std::invalid_argument("RLGL: invalid SetDataOptions ordinal");
                }
                EnsureStorage(stride);

                if (options == SetDataOptions::Discard)
                {
                    Bridge::OrphanBuffer(
                        id_, false, CheckedByteCapacity(capacity_, stride_, "vertex-buffer"));
                    std::fill(cpuBytes_.begin(), cpuBytes_.end(), 0u);
                    ++discardUploadCount_;
                }
                else if (options == SetDataOptions::NoOverwrite)
                    ++noOverwriteUploadCount_;
                else
                    ++ordinaryUploadCount_;

                Bridge::UpdateVertexBuffer(id_, data, byteCount);
                std::memcpy(cpuBytes_.data(), data, static_cast<std::size_t>(byteCount));
                vertexCount_ = vertexCount;
            }

            unsigned int id_ = 0;
            int capacity_ = 0;
            int vertexCount_ = 0;
            std::size_t stride_ = 0;
            std::vector<std::uint8_t> cpuBytes_;
            std::vector<VertexElement> declaration_;
            int ordinaryUploadCount_ = 0;
            int discardUploadCount_ = 0;
            int noOverwriteUploadCount_ = 0;
        };

        class RlglIndexBufferRenderer final : public IIndexBufferRenderer
        {
        public:
            RlglIndexBufferRenderer(const int indexCapacity, const bool thirtyTwoBit)
                : capacity_(indexCapacity)
                , thirtyTwoBit_(thirtyTwoBit)
                , elementSize_(thirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t))
            {
                const int byteCapacity =
                    CheckedByteCapacity(capacity_, elementSize_, "index-buffer");
                id_ = Bridge::CreateIndexBuffer(byteCapacity);
                cpuBytes_.assign(static_cast<std::size_t>(byteCapacity), 0u);
            }

            ~RlglIndexBufferRenderer() override
            {
                Bridge::DestroyBuffer(id_);
            }

            void SetData16(const void* const data, const int indexCount) override
            {
                Upload(data, indexCount, false, SetDataOptions::None);
            }

            void SetData32(const void* const data, const int indexCount) override
            {
                Upload(data, indexCount, true, SetDataOptions::None);
            }

            void SetData16WithOptions(
                const void* const data, const int indexCount,
                const SetDataOptions options) override
            {
                Upload(data, indexCount, false, options);
            }

            void SetData32WithOptions(
                const void* const data, const int indexCount,
                const SetDataOptions options) override
            {
                Upload(data, indexCount, true, options);
            }

            [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
            [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }
            [[nodiscard]] unsigned int NativeId() const noexcept { return id_; }
            [[nodiscard]] int Capacity() const noexcept { return capacity_; }

            [[nodiscard]] BufferResourceSnapshot Snapshot() const
            {
                BufferResourceSnapshot result;
                result.id = id_;
                result.capacity = capacity_;
                result.count = indexCount_;
                result.stride = elementSize_;
                result.indexBuffer = true;
                result.thirtyTwoBit = thirtyTwoBit_;
                result.cpuBytes = cpuBytes_;
                const Bridge::BufferSnapshot native =
                    Bridge::GetBufferSnapshotForTesting(id_, true);
                result.nativeByteSize = native.byteSize;
                result.nativeUsage = native.usage;
                result.nativeBytes = native.bytes;
                result.ordinaryUploadCount = ordinaryUploadCount_;
                result.discardUploadCount = discardUploadCount_;
                result.noOverwriteUploadCount = noOverwriteUploadCount_;
                return result;
            }

        private:
            void Upload(
                const void* const data, const int indexCount,
                const bool thirtyTwoBit, const SetDataOptions options)
            {
                if (thirtyTwoBit != thirtyTwoBit_)
                    throw std::invalid_argument(
                        "RLGL: index upload width does not match the index buffer");
                const int byteCount = CheckedUploadBytes(
                    indexCount, capacity_, elementSize_, data, "index-buffer");
                if (byteCount == 0) return;
                if (options != SetDataOptions::None &&
                    options != SetDataOptions::Discard &&
                    options != SetDataOptions::NoOverwrite)
                {
                    throw std::invalid_argument("RLGL: invalid SetDataOptions ordinal");
                }

                if (options == SetDataOptions::Discard)
                {
                    Bridge::OrphanBuffer(
                        id_, true,
                        CheckedByteCapacity(capacity_, elementSize_, "index-buffer"));
                    std::fill(cpuBytes_.begin(), cpuBytes_.end(), 0u);
                    ++discardUploadCount_;
                }
                else if (options == SetDataOptions::NoOverwrite)
                    ++noOverwriteUploadCount_;
                else
                    ++ordinaryUploadCount_;

                Bridge::UpdateIndexBuffer(id_, data, byteCount);
                std::memcpy(cpuBytes_.data(), data, static_cast<std::size_t>(byteCount));
                indexCount_ = indexCount;
            }

            unsigned int id_ = 0;
            int capacity_ = 0;
            int indexCount_ = 0;
            bool thirtyTwoBit_ = false;
            std::size_t elementSize_ = 0;
            std::vector<std::uint8_t> cpuBytes_;
            int ordinaryUploadCount_ = 0;
            int discardUploadCount_ = 0;
            int noOverwriteUploadCount_ = 0;
        };
    }

    std::unique_ptr<IVertexBufferRenderer> CreateVertexBufferRenderer(
        const int vertexCapacity)
    {
        return std::make_unique<RlglVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> CreateIndexBufferRenderer(
        const int indexCapacity, const bool thirtyTwoBit)
    {
        return std::make_unique<RlglIndexBufferRenderer>(indexCapacity, thirtyTwoBit);
    }

    BufferResourceSnapshot GetBufferResourceSnapshotForTesting(
        const IVertexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglVertexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: vertex resource belongs to another renderer");
        return buffer->Snapshot();
    }

    BufferResourceSnapshot GetBufferResourceSnapshotForTesting(
        const IIndexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglIndexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: index resource belongs to another renderer");
        return buffer->Snapshot();
    }

    unsigned int GetNativeBufferId(const IVertexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglVertexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: vertex resource belongs to another renderer");
        return buffer->NativeId();
    }

    unsigned int GetNativeBufferId(const IIndexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglIndexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: index resource belongs to another renderer");
        return buffer->NativeId();
    }

    std::size_t GetVertexStride(const IVertexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglVertexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: vertex resource belongs to another renderer");
        return buffer->Stride();
    }

    int GetBufferCapacity(const IVertexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglVertexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: vertex resource belongs to another renderer");
        return buffer->Capacity();
    }

    int GetBufferCapacity(const IIndexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglIndexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: index resource belongs to another renderer");
        return buffer->Capacity();
    }

    const std::vector<VertexElement>& GetVertexDeclaration(
        const IVertexBufferRenderer& resource)
    {
        const auto* const buffer = dynamic_cast<const RlglVertexBufferRenderer*>(&resource);
        if (buffer == nullptr)
            throw std::invalid_argument("RLGL: vertex resource belongs to another renderer");
        return buffer->Declaration();
    }

    VertexAttributeBinding DescribeVertexAttribute(
        const VertexElement& element, const unsigned int location,
        const int stride, const int baseOffset)
    {
        if (stride <= 0 || baseOffset < 0 || element.getOffsetProperty() < 0)
        {
            throw std::invalid_argument("RLGL: invalid vertex attribute byte range");
        }

        VertexAttributeBinding result;
        result.location = location;
        result.stride = stride;
        int scalarSize = 0;
        switch (element.getVertexElementFormatProperty())
        {
        case VertexElementFormat::Single:
            result.componentCount = 1; result.scalarType = 0x1406; scalarSize = 4; break;
        case VertexElementFormat::Vector2:
            result.componentCount = 2; result.scalarType = 0x1406; scalarSize = 4; break;
        case VertexElementFormat::Vector3:
            result.componentCount = 3; result.scalarType = 0x1406; scalarSize = 4; break;
        case VertexElementFormat::Vector4:
            result.componentCount = 4; result.scalarType = 0x1406; scalarSize = 4; break;
        case VertexElementFormat::Color:
            result.componentCount = 4; result.scalarType = 0x1401;
            result.normalized = true; scalarSize = 1; break;
        case VertexElementFormat::Byte4:
            result.componentCount = 4; result.scalarType = 0x1401; scalarSize = 1; break;
        case VertexElementFormat::Short2:
            result.componentCount = 2; result.scalarType = 0x1402; scalarSize = 2; break;
        case VertexElementFormat::Short4:
            result.componentCount = 4; result.scalarType = 0x1402; scalarSize = 2; break;
        case VertexElementFormat::NormalizedShort2:
            result.componentCount = 2; result.scalarType = 0x1402;
            result.normalized = true; scalarSize = 2; break;
        case VertexElementFormat::NormalizedShort4:
            result.componentCount = 4; result.scalarType = 0x1402;
            result.normalized = true; scalarSize = 2; break;
        case VertexElementFormat::HalfVector2:
            result.componentCount = 2; result.scalarType = 0x140B; scalarSize = 2; break;
        case VertexElementFormat::HalfVector4:
            result.componentCount = 4; result.scalarType = 0x140B; scalarSize = 2; break;
        default:
            throw std::invalid_argument("RLGL: invalid VertexElementFormat ordinal");
        }
        const int elementSize = result.componentCount * scalarSize;
        if (element.getOffsetProperty() > stride - elementSize ||
            baseOffset > std::numeric_limits<int>::max() - element.getOffsetProperty())
        {
            throw std::invalid_argument("RLGL: invalid vertex attribute byte range");
        }
        result.offset = baseOffset + element.getOffsetProperty();
        return result;
    }
}
