// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
    class IndexBuffer;
    class ModelMesh;
    class VertexBuffer;
    class VertexDeclaration;

    /**
     * @brief Represents a batch of geometry using the same effect within a ModelMesh.
     */
    class ModelMeshPart
    {
    public:
        /** @brief Constructs an empty mesh part. */
        ModelMeshPart() = default;

        /**
         * @brief Constructs a mesh part with explicit buffer and count parameters.
         * @param vb The vertex buffer for this mesh part.
         * @param ib The index buffer for this mesh part.
         * @param numVertices The number of vertices used during a draw call.
         * @param primitiveCount The number of primitives to render.
         * @param startIndex The location in the index buffer at which to start reading.
         * @param vertexOffset The offset in vertices from the top of the vertex buffer.
         */
        CNAEXT ModelMeshPart(VertexBuffer* vb, IndexBuffer* ib,
                            int numVertices, int primitiveCount,
                            int startIndex, int vertexOffset);

        /**
         * @brief Gets the number of vertices used during a draw call.
         * @return The vertex count.
         */
        [[nodiscard]] int getNumVerticesProperty() const;

        /**
         * @brief Gets the number of primitives to render.
         *
         * Primitives of **this part's own topology**, not triangles: a `LineList` part of `n`
         * indices has `n / 2` primitives, a `LineStrip` has `n - 1`, and a `PointListEXT` has `n`
         * (plans/plan_gltf.md §12.3, `GLTF-078`). For the `TriangleList` default the value is `n / 3`, as
         * it has always been, so a caller that only ever handles triangle lists is unaffected —
         * but `primitiveCount * 3 == indexCount` is **not** a safe assumption in general.
         *
         * @return The primitive count.
         */
        [[nodiscard]] int getPrimitiveCountProperty() const;

        /**
         * @brief Gets the topology this part's index buffer describes.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Real XNA carries the topology as an argument
         * to `GraphicsDevice::DrawIndexedPrimitives`, so every XNA `ModelMeshPart` is implicitly a
         * triangle list. glTF's own primitive modes need somewhere to survive between the importer
         * and the draw (plans/plan_gltf.md §10.1, `GLTF-073`), and this is that place. Defaults to
         * `TriangleList`, which is what every part built by any other path already is.
         *
         * @return The topology to draw this part with.
         */
        CNAEXT [[nodiscard]] PrimitiveType getPrimitiveTypeEXTProperty() const;

        /**
         * @brief Sets the topology this part's index buffer describes.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Setting this does not reinterpret the index
         * buffer; it states what the buffer already contains.
         *
         * @param value The topology to draw this part with.
         */
        CNAEXT void setPrimitiveTypeEXTProperty(PrimitiveType value);

        /**
         * @brief Per-texture-slot sampler state imported from the source asset.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-202`/`GLTF-207`). XNA has
         * no per-part sampler concept: `SamplerState` is device state the application sets before a
         * draw, and a part that came from a file carrying `CLAMP_TO_EDGE` had nowhere to say so —
         * every imported texture was drawn with whatever the device happened to have, which
         * defaults to `LinearWrap`. For an asset with UVs outside `[0,1]` that is a large, visible
         * error rather than a subtle one.
         *
         * A property rather than a `Tag` payload, which is what `GLTF-207` originally sketched:
         * `ModelMeshPart::Tag` already carries `MorphTargetDataEXT`, so a morphing part with
         * clamped textures could not have expressed both.
         *
         * Indexed by texture slot in the same order the importer uses (base colour, normal,
         * metallic-roughness, emissive, occlusion). Every entry is `LinearWrap` for a part built by
         * any other content path, so this changes nothing for existing content.
         *
         * @return The part's sampler states, one per texture slot.
         */
        CNAEXT [[nodiscard]] const std::array<SamplerState, 5>& getSamplerStatesEXTProperty() const;

        /**
         * @brief Sets one texture slot's sampler state.
         *
         * @param slot The texture slot index, in the importer's own order; out of range is ignored.
         * @param value The sampler state to apply when this part's texture is bound to that slot.
         */
        CNAEXT void setSamplerStateEXTProperty(int slot, const SamplerState& value);

        /**
         * @brief Samplers for `KHR_materials_specular` strength then colour maps.
         *
         * Kept separate from the established five-entry core PBR array so adding the extension
         * does not change that public property's ABI or slot meanings. Both entries default to
         * `LinearWrap`.
         */
        CNAEXT [[nodiscard]] const std::array<SamplerState, 2>&
        getSpecularSamplerStatesEXTProperty() const;

        /** @brief Sets extension sampler slot 0 (strength) or 1 (colour); others are ignored. */
        CNAEXT void setSpecularSamplerStateEXTProperty(int slot, const SamplerState& value);

        /**
         * @brief Gets the location in the index array at which to start reading vertices.
         * @return The start index.
         */
        [[nodiscard]] int getStartIndexProperty() const;

        /**
         * @brief Gets the offset in vertices from the top of the vertex buffer.
         * @return The vertex offset.
         */
        [[nodiscard]] int getVertexOffsetProperty() const;

        /**
         * @brief Gets the material Effect for this mesh part.
         * @return Pointer to the Effect, or nullptr if none is set.
         */
        [[nodiscard]] Effect* getEffectProperty() const;

        /**
         * @brief Sets the material Effect for this mesh part.
         * @param value Pointer to the new Effect.
         */
        void setEffectProperty(Effect* value);

        /**
         * @brief Gets the index buffer for this mesh part.
         * @return Pointer to the IndexBuffer.
         */
        [[nodiscard]] IndexBuffer* getIndexBufferProperty() const;

        /**
         * @brief Gets the vertex buffer for this mesh part.
         * @return Pointer to the VertexBuffer.
         */
        [[nodiscard]] VertexBuffer* getVertexBufferProperty() const;

        /**
         * @brief Gets the custom object attached to this mesh part.
         * @return Pointer to the tag object, or nullptr.
         */
        [[nodiscard]] System::Object* getTagProperty() const;

        /**
         * @brief Sets the custom object attached to this mesh part.
         * @param value Pointer to the tag object.
         */
        void setTagProperty(System::Object* value);

        /**
         * @brief Sets the offset in vertices from the top of the vertex buffer.
         *
         * CNAEXT: real XNA's `ModelMeshPart.VertexOffset` setter is content-pipeline-only
         * (`internal set`) rather than a public game-facing API; the content pipeline is the only
         * real caller (plans/plan_xnb.md XNB-38's `ModelReader`), so this stays a CNAEXT-marked method
         * rather than a bare public setter.
         * @param value The new vertex offset.
         */
        CNAEXT void SetVertexOffset(int value);

        /**
         * @brief Sets the number of vertices used during a draw call. See `SetVertexOffset()`'s
         *        own note on why this is CNAEXT rather than a bare public setter.
         * @param value The new vertex count.
         */
        CNAEXT void SetNumVertices(int value);

        /**
         * @brief Sets the location in the index buffer at which to start reading. See
         *        `SetVertexOffset()`'s own note on why this is CNAEXT rather than a bare public setter.
         * @param value The new start index.
         */
        CNAEXT void SetStartIndex(int value);

        /**
         * @brief Sets the number of primitives to render. See `SetVertexOffset()`'s own note on
         *        why this is CNAEXT rather than a bare public setter.
         * @param value The new primitive count.
         */
        CNAEXT void SetPrimitiveCount(int value);

        /**
         * @brief Sets the vertex buffer for this mesh part. See `SetVertexOffset()`'s own note on
         *        why this is CNAEXT rather than a bare public setter.
         * @param value Pointer to the new VertexBuffer.
         */
        CNAEXT void SetVertexBuffer(VertexBuffer* value);

        /**
         * @brief Sets the index buffer for this mesh part. See `SetVertexOffset()`'s own note on
         *        why this is CNAEXT rather than a bare public setter.
         * @param value Pointer to the new IndexBuffer.
         */
        CNAEXT void SetIndexBuffer(IndexBuffer* value);

    private:
        int numVertices_    = 0;
        int primitiveCount_ = 0;
        PrimitiveType primitiveType_ = PrimitiveType::TriangleList;
        std::array<SamplerState, 5> samplerStates_{};
        std::array<SamplerState, 2> specularSamplerStatesEXT_{};
        int startIndex_     = 0;
        int vertexOffset_   = 0;
        Effect* effect_     = nullptr;
        IndexBuffer* indexBuffer_   = nullptr;
        VertexBuffer* vertexBuffer_ = nullptr;
        System::Object* tag_        = nullptr;
        ModelMesh* parent_          = nullptr;

        friend class ModelMesh;
    };
}
