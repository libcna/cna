// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

// plans/plan_xnb.md XNB-36/37/38/39/40: Model and shared-resource-heavy readers -- see
// PrimitiveContentTypeReaders.hpp's own note on why these live in CNA::Internal::Xnb (FNA's own
// equivalents are all `internal`/default-visibility classes, never subclassed by game code).
//
// VertexBufferReader/IndexBufferReader target std::shared_ptr<T> rather than a bare T: both
// VertexBuffer and IndexBuffer are move-only (matching CNA's established GPU-resource-ownership
// design), and -- distinctly from that -- they are genuinely *shared* resources in a Model's own
// object graph (plans/plan_xnb.md XNB-40: multiple ModelMeshParts can reference the exact same buffer),
// which is what ReadSharedResource<T>() exists for in the first place. VertexDeclarationReader
// stays a bare ContentTypeReader<VertexDeclaration> (copy-constructible, never a shared resource --
// read once per VertexBuffer via ReadRawObject, matching FNA exactly) and ModelReader stays a bare
// ContentTypeReader<Model> (Model itself is copy-constructible; its actual GPU resources are kept
// alive via Model::setOwnedResources(), not via Model's own copyability).

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;
    using Microsoft::Xna::Framework::Graphics::IndexBuffer;
    using Microsoft::Xna::Framework::Graphics::Model;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.VertexDeclarationReader`. */
    class VertexDeclarationReader : public ContentTypeReader<VertexDeclaration>
    {
    public:
        VertexDeclarationReader()
            : ContentTypeReader<VertexDeclaration>("Microsoft.Xna.Framework.Graphics.VertexDeclaration") {}

    protected:
        VertexDeclaration Read(ContentReader& input, std::optional<VertexDeclaration> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.VertexBufferReader`. */
    class VertexBufferReader : public ContentTypeReader<std::shared_ptr<VertexBuffer>>
    {
    public:
        VertexBufferReader()
            : ContentTypeReader<std::shared_ptr<VertexBuffer>>("Microsoft.Xna.Framework.Graphics.VertexBuffer") {}

    protected:
        std::shared_ptr<VertexBuffer> Read(
            ContentReader& input, std::optional<std::shared_ptr<VertexBuffer>> existingInstance) override;
    };

    /** @brief FNA's real `Microsoft.Xna.Framework.Content.IndexBufferReader`. */
    class IndexBufferReader : public ContentTypeReader<std::shared_ptr<IndexBuffer>>
    {
    public:
        IndexBufferReader()
            : ContentTypeReader<std::shared_ptr<IndexBuffer>>("Microsoft.Xna.Framework.Graphics.IndexBuffer") {}

    protected:
        std::shared_ptr<IndexBuffer> Read(
            ContentReader& input, std::optional<std::shared_ptr<IndexBuffer>> existingInstance) override;
    };

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.ModelReader`
     *        (`src/Content/ContentReaders/ModelReader.cs`).
     *
     * **Current coverage**: bone name/transform/hierarchy (multi-bone, plans/plan_xnb.md XNB-37), the
     * root bone, per-mesh name/`ParentBone`/`BoundingSphere` (XNB-38/39), and per-mesh-part
     * scalar fields plus `VertexBuffer`/`IndexBuffer`/`Effect` shared-resource dedup (XNB-40).
     * `Model`/mesh/mesh-part `Tag` fields retain custom reference objects and the stock-pipeline
     * dictionary shapes currently registered by CNA. Unsupported non-null erasure shapes throw a
     * clear `ContentLoadException` rather than silently dropping data. Reloading into an
     * `existingInstance` is not supported (`CanDeserializeIntoExistingObject` stays false,
     * matching every other reader in this plan).
     */
    class ModelReader : public ContentTypeReader<Model>
    {
    public:
        ModelReader() : ContentTypeReader<Model>("Microsoft.Xna.Framework.Graphics.Model") {}

    protected:
        Model Read(ContentReader& input, std::optional<Model> existingInstance) override;
    };

    /** @brief Registers VertexDeclarationReader/VertexBufferReader/IndexBufferReader/ModelReader under their real FNA canonical names. Idempotent. */
    void RegisterModelXnbReaders();
}
