// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include <Magnum/GL/Attribute.h>
#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/SampleQuery.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Magnum
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
        /**
         * @brief True when the element must reach the shader as a real integer.
         *
         * Only a stock layout sets this, for `SkinnedEffect`'s bone indices: they index a uniform
         * array, so converting them to float and back would lose exactness for the higher bone
         * numbers. A declaration-driven element never sets it -- XNA's `Byte4` is a float-converted
         * format everywhere else it appears.
         */
        bool integral = false;
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
     * CNA renderer selects an input layout: 16 = `VertexPositionColor`, 20 = `VertexPositionTexture`,
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

    /**
     * @brief A vertex array configuration cached against the draw that produced it.
     *
     * Building a `GL::Mesh` creates a vertex array object and re-specifies every attribute pointer,
     * which is real per-draw cost for a configuration that almost never changes between frames.
     * The entry is keyed by everything the binding depends on -- each stream's buffer identity,
     * declaration revision, stride, binding offset and instance frequency, plus the index buffer's
     * identity and element size -- so a key match means the very same vertex array would have been
     * rebuilt byte for byte.
     *
     * Identity is a monotonic counter rather than a pointer, because a destroyed buffer's address
     * can be reused by a later one: keying on the address alone would let a stale entry match a
     * different buffer and silently draw the wrong data.
     */
    struct MagnumMeshCacheEntry
    {
        /** @brief The binding configuration this vertex array was built for. */
        std::vector<std::uint64_t> key;
        /** @brief The vertex array itself. */
        std::unique_ptr<Mg::GL::Mesh> mesh;
    };

    /** @brief Magnum-backed vertex buffer. */
    class MagnumVertexBufferRenderer : public IVertexBufferRenderer
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
        explicit MagnumVertexBufferRenderer(int vertexCapacity);
        ~MagnumVertexBufferRenderer() override = default;

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
        /** @brief This buffer's process-unique identity, never reused by a later buffer. */
        [[nodiscard]] std::uint64_t GetIdentity() const { return identity_; }
        /** @brief Bumped whenever `SetVertexDeclaration` replaces the recorded elements. */
        [[nodiscard]] std::uint64_t GetDeclarationRevision() const { return declarationRevision_; }

        /**
         * @brief Looks up a previously built vertex array for this binding configuration.
         *
         * The cache lives on the buffer rather than on the graphics renderer so that a destroyed
         * buffer takes its own vertex arrays with it -- a cache outliving the buffer it binds would
         * hand out arrays pointing at deleted storage.
         *
         * @param key Binding configuration to match.
         * @return The cached vertex array, or nullptr when none matches.
         */
        [[nodiscard]] Mg::GL::Mesh* FindCachedMesh(const std::vector<std::uint64_t>& key) const;

        /**
         * @brief Stores a freshly built vertex array against its binding configuration.
         *
         * @param key  Binding configuration the array was built for.
         * @param mesh The array itself.
         * @return A reference to the stored array.
         */
        Mg::GL::Mesh& StoreCachedMesh(std::vector<std::uint64_t> key,
                                      std::unique_ptr<Mg::GL::Mesh> mesh) const;

    private:
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options);

        std::unique_ptr<Mg::GL::Buffer> buffer_;
        std::uint64_t identity_ = 0;
        std::uint64_t declarationRevision_ = 0;
        /**
         * Bounded so a draw sweeping its start vertex cannot grow this without limit; the oldest
         * entry is evicted at the cap, which degrades to rebuilding per draw -- the behaviour
         * before this cache existed -- rather than to anything worse.
         */
        mutable std::vector<MagnumMeshCacheEntry> meshCache_;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t strideInBytes_ = 0;
        std::size_t allocatedBytes_ = 0;
        std::vector<VertexElement> declarationElements_;
    };

    /** @brief Magnum-backed 16- or 32-bit index buffer. */
    class MagnumIndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates an unallocated index buffer.
         *
         * @param indexCapacity Indices the owning `IndexBuffer` was declared with.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        MagnumIndexBufferRenderer(int indexCapacity, bool thirtyTwoBit);
        ~MagnumIndexBufferRenderer() override = default;

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
        /** @brief This buffer's process-unique identity, never reused by a later buffer. */
        [[nodiscard]] std::uint64_t GetIdentity() const { return identity_; }

    private:
        void Upload(const void* data, int indexCount, int indexSize, SetDataOptions options);

        std::unique_ptr<Mg::GL::Buffer> buffer_;
        std::uint64_t identity_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::size_t allocatedBytes_ = 0;
    };

    /** @brief Magnum-backed occlusion query. */
    class MagnumOcclusionQueryRenderer : public IOcclusionQueryRenderer
    {
    public:
        /** @brief Creates a samples-passed query object. */
        MagnumOcclusionQueryRenderer();
        ~MagnumOcclusionQueryRenderer() override = default;

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

    /**
     * @brief CNAEXT. REMED-GFX-DECL-GUARD: refuses a declaration the stock route would misread.
     *
     * The stock route resolves its attribute layout from `StockAttributesForStride()` -- the
     * combined byte stride alone -- and its program's input semantics are fixed by that layout, so
     * a custom declaration packing different semantics into one of the known stock widths would be
     * read from the wrong bytes and rendered without any error. The custom-effect route is not
     * guarded: it binds each declared element from the declaration itself
     * (`AttributesForDeclaration()`), so it is faithful by construction.
     *
     * The check is **asymmetric** -- only what the caller actually declared is verified, never
     * equality against the stock template -- so a declaration that omits attributes the template
     * carries still draws. It is pure: nothing is created, queued or bound before it runs, so a
     * rejected draw leaves the device untouched. An unlisted stride is left to the stock program
     * selection's own refusal.
     *
     * Multi-stream draws are checked over the union of the per-vertex streams' declared elements,
     * lifted into the combined record the stock template describes: a stream's declaration is
     * stream-local (its `AlignedByteOffset`s start at 0, exactly as FNA3D consumes them), so each
     * element is compared at `combinedByteBase + offset` -- the same mapping
     * `MapCombinedOffsetToStream()` inverts when the stock layout is re-slotted. Per-instance
     * streams carry instance records, not part of the combined per-vertex layout, and are skipped.
     *
     * Header-only by necessity: `cna_renderer_magnum` links only
     * `cna_renderer_common` and SharpRuntime, never the CNA library.
     *
     * @param vb             The draw's principal vertex buffer.
     * @param params         The draw's shared parameters, for the bound stream set.
     * @param combinedStride The combined byte stride the stock layout is resolved from.
     * @param route          Name of the draw route, for the diagnostic message.
     * @throws System::NotSupportedException When the declaration cannot be represented.
     */
    inline void RequireFaithfulDeclarationEXT(const MagnumVertexBufferRenderer& vb,
                                              const GpuDrawParams& params,
                                              std::size_t combinedStride, const char* route)
    {
        namespace fid = CNA::Internal::Graphics;

        std::vector<VertexElement> declared;
        if (params.vertexStreamCount > 0)
        {
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                if (stream.instanceFrequency > 0)
                    continue;
                const auto* streamBuffer =
                    dynamic_cast<const MagnumVertexBufferRenderer*>(stream.buffer);
                if (streamBuffer == nullptr)
                    continue;
                for (const VertexElement& element : streamBuffer->GetDeclarationElements())
                {
                    declared.emplace_back(
                        element.getOffsetProperty() + stream.combinedByteBase,
                        element.getVertexElementFormatProperty(),
                        element.getVertexElementUsageProperty(),
                        element.getUsageIndexProperty());
                }
            }
        }
        else
        {
            declared = vb.GetDeclarationElements();
        }
        if (declared.empty())
            return;

        const int stride = static_cast<int>(combinedStride);
        const fid::InferredVertexLayout inferred =
            fid::InferredLayoutForStride(stride, fid::UnlistedStrideLayout::RendererRefusesIt);
        const std::string failure =
            fid::DescribeUnrepresentableVertexDeclaration(declared, stride, stride, inferred);
        if (failure.empty())
            return;
        throw System::NotSupportedException(
            std::string("Magnum: this VertexDeclaration cannot be represented on the ") + route +
            " route -- " + failure +
            ". The stock route selects its native vertex layout from the buffer stride and does "
            "not translate arbitrary declarations, so the draw is refused rather than rendered "
            "from the wrong bytes.");
    }
}
