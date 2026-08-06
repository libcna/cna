// SPDX-License-Identifier: MS-PL
#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <Magnum/GL/Attribute.h>
#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/SampleQuery.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Magnum
{
    namespace Mg = ::Magnum;

    /**
     * @brief Describes one vertex attribute of a layout, resolved at runtime.
     *
     * CNA dispatches its stock shaders on a vertex's byte stride and lets `ShaderEffect` bind a
     * caller-authored `VertexDeclaration`, so no layout here is known at compile time. This is the
     * intermediate form both routes produce and `GL::DynamicAttribute` is built from.
     */
    struct MagnumVertexAttribute
    {
        /** @brief Shader attribute location this element feeds. */
        int location = 0;
        /** @brief Byte offset of the element inside its own stream's vertex. */
        int offsetInStream = 0;
        /** @brief Component count (1..4). */
        int components = 4;
        /** @brief True when the source is integral data normalized into [0,1] (or [-1,1]). */
        bool normalized = false;
        /** @brief Raw `VertexElementFormat` ordinal this element was declared with. */
        int format = 0;
    };

    /**
     * @brief Builds the Magnum attribute description for one resolved element.
     *
     * @param attribute Element to describe.
     * @return A `GL::DynamicAttribute` naming the same location, component count and data type.
     */
    [[nodiscard]] Mg::GL::DynamicAttribute ToDynamicAttribute(const MagnumVertexAttribute& attribute);

    /**
     * @brief Resolves the attribute list CNA's stock shaders expect for a given vertex stride.
     *
     * The four built-in XNA vertex types are identified by their stride, matching how every other
     * CNA backend selects an input layout: 16 = `VertexPositionColor`, 20 = `VertexPositionTexture`,
     * 24 = `VertexPositionColorTexture`, 32 = `VertexPositionNormalTexture`.
     *
     * @param strideInBytes Byte stride of one combined vertex.
     * @return The attributes for that stride, or an empty list for an unrecognized stride.
     */
    [[nodiscard]] std::vector<MagnumVertexAttribute> StockAttributesForStride(
        std::size_t strideInBytes);

    /**
     * @brief Resolves the attribute list a caller-supplied declaration describes.
     *
     * Locations are assigned in declaration order, which is the convention a `ShaderEffect`'s own
     * GLSL is expected to declare its inputs with.
     *
     * @param elements     Declaration elements, in declaration order.
     * @param baseLocation First shader location to assign.
     * @param byteBase     Combined-layout byte offset at which this stream's declaration begins,
     *                     subtracted so the results are stream-local.
     * @return One attribute per element.
     */
    [[nodiscard]] std::vector<MagnumVertexAttribute> AttributesForDeclaration(
        const std::vector<VertexElement>& elements, int baseLocation, int byteBase);

    /** @brief Magnum-backed vertex buffer. */
    class MagnumVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        /**
         * @brief Creates an unallocated buffer sized for @p vertexCapacity vertices.
         *
         * The byte size cannot be known until the first `SetData` reveals the stride, so GL storage
         * is allocated there rather than here.
         *
         * @param vertexCapacity Vertex elements the owning `VertexBuffer` was declared with.
         */
        explicit MagnumVertexBufferBackend(int vertexCapacity);
        ~MagnumVertexBufferBackend() override = default;

        /**
         * @brief Uploads packed vertex data, replacing the buffer's contents.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        /**
         * @brief Uploads packed vertex data with a streaming hint.
         *
         * `Discard` orphans the previous allocation so the driver need not stall on in-flight
         * draws; the other options write into the existing allocation.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         * @param options       Streaming hint.
         */
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes,
                                SetDataOptions options) override;
        /**
         * @brief Records the caller's complete declaration so custom layouts bind generically.
         *
         * @param vertexDeclaration Full declaration, including stride and elements in order.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;
        /** @brief Vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Byte stride of one vertex, as revealed by the most recent upload. */
        [[nodiscard]] std::size_t GetStride() const { return strideInBytes_; }
        /** @brief Declaration elements recorded by `SetVertexDeclaration`; empty when unset. */
        [[nodiscard]] const std::vector<VertexElement>& GetDeclarationElements() const
        {
            return declarationElements_;
        }
        /** @brief The underlying Magnum buffer, so a draw path can bind it into a mesh. */
        [[nodiscard]] Mg::GL::Buffer& GetBuffer() const { return *buffer_; }

    private:
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options);

        std::unique_ptr<Mg::GL::Buffer> buffer_;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t strideInBytes_ = 0;
        std::size_t allocatedBytes_ = 0;
        std::vector<VertexElement> declarationElements_;
    };

    /** @brief Magnum-backed 16- or 32-bit index buffer. */
    class MagnumIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        /**
         * @brief Creates an unallocated index buffer.
         *
         * @param indexCapacity Indices the owning `IndexBuffer` was declared with.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        MagnumIndexBufferBackend(int indexCapacity, bool thirtyTwoBit);
        ~MagnumIndexBufferBackend() override = default;

        /**
         * @brief Uploads 16-bit indices, replacing the buffer's contents.
         *
         * @param data       Packed `uint16_t` indices.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;
        /**
         * @brief Uploads 32-bit indices, replacing the buffer's contents.
         *
         * @param data       Packed `uint32_t` indices.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;
        /**
         * @brief Uploads 16-bit indices with a streaming hint.
         *
         * @param data       Packed `uint16_t` indices.
         * @param indexCount Number of indices.
         * @param options    Streaming hint.
         */
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        /**
         * @brief Uploads 32-bit indices with a streaming hint.
         *
         * @param data       Packed `uint32_t` indices.
         * @param indexCount Number of indices.
         * @param options    Streaming hint.
         */
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        /** @brief Indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief True when this buffer holds 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Bytes one index occupies (2 or 4). */
        [[nodiscard]] int GetIndexSize() const { return thirtyTwoBit_ ? 4 : 2; }
        /** @brief The underlying Magnum buffer, so a draw path can bind it into a mesh. */
        [[nodiscard]] Mg::GL::Buffer& GetBuffer() const { return *buffer_; }

    private:
        void Upload(const void* data, int indexCount, int indexSize, SetDataOptions options);

        std::unique_ptr<Mg::GL::Buffer> buffer_;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::size_t allocatedBytes_ = 0;
    };

    /** @brief Magnum-backed occlusion query. */
    class MagnumOcclusionQueryBackend : public IOcclusionQueryBackend
    {
    public:
        /** @brief Creates a samples-passed query object. */
        MagnumOcclusionQueryBackend();
        ~MagnumOcclusionQueryBackend() override = default;

        /** @brief Starts counting samples that pass the depth test. */
        void Begin() override;
        /** @brief Stops counting. */
        void End() override;
        /** @brief True once the driver has the counted result ready. */
        [[nodiscard]] bool IsComplete() const override;
        /** @brief Samples counted between `Begin()` and `End()`; blocks until the result is ready. */
        [[nodiscard]] int PixelCount() const override;

    private:
        std::unique_ptr<Mg::GL::SampleQuery> query_;
        bool started_ = false;
    };
}
