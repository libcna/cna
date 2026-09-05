// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MaterialContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexCollections.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/VertexBufferContent.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    class ModelBoneContent;

    /** @brief A read-only list of bones, as XNA's ReadOnlyCollection is. */
    using ModelBoneContentCollection = std::vector<std::shared_ptr<ModelBoneContent>>;

    /**
     * @brief One bone of a processed model: its place in the skeleton and its transform.
     *
     * Every node of the scene becomes a bone, indexed in the order the processor walks them
     * (measured, `tests/reference/xna40/graphics/graphics-content-oracle.json`,
     * `modelprocessor/bone_hierarchy`).
     */
    class ModelBoneContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelBoneContent";

        /**
         * @brief Initializes a bone.
         *
         * XNA creates these only through the model processor; the constructor is CNAEXT for the
         * same reason.
         *
         * @param name The bone's name.
         * @param index The bone's index in the model.
         * @param transform The bone's transform, relative to its parent.
         * @param parent The bone above this one, or null for the root.
         */
        CNAEXT ModelBoneContent(std::string name, SharpRuntime::intcs index, Matrix transform,
                                std::shared_ptr<ModelBoneContent> parent);

        /**
         * @brief Gets the bones hanging from this one.
         * @return The children.
         */
        [[nodiscard]] const ModelBoneContentCollection& getChildrenProperty() const noexcept;

        /**
         * @brief Gets the bone's index in the model's bone list.
         * @return The index.
         */
        [[nodiscard]] SharpRuntime::intcs getIndexProperty() const noexcept;

        /**
         * @brief Gets the bone's name.
         * @return The name.
         */
        [[nodiscard]] const std::string& getNameProperty() const noexcept;

        /**
         * @brief Gets the bone above this one.
         * @return The parent, or null for the root.
         */
        [[nodiscard]] std::shared_ptr<ModelBoneContent> getParentProperty() const noexcept;

        /**
         * @brief Gets the bone's transform, relative to its parent.
         * @return The transform.
         */
        [[nodiscard]] const Matrix& getTransformProperty() const noexcept;

        /**
         * @brief Sets the bone's transform, relative to its parent.
         * @param value The transform.
         */
        void setTransformProperty(Matrix value) noexcept;

        /**
         * @brief Adds a child bone, which only the model processor does.
         * @param child The bone to add.
         */
        CNAEXT void AddChild(std::shared_ptr<ModelBoneContent> child);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string name_;
        SharpRuntime::intcs index_ = 0;
        Matrix transform_;
        std::weak_ptr<ModelBoneContent> parent_;
        ModelBoneContentCollection children_;
    };

    /**
     * @brief One drawable piece of a processed mesh: a slice of an index buffer over a vertex
     *        buffer, drawn with one material.
     */
    class ModelMeshPartContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelMeshPartContent";

        /**
         * @brief Initializes a mesh part; only the model processor does.
         *
         * @param vertexBuffer The vertices this part draws from.
         * @param indexBuffer The indices this part draws with.
         * @param vertexOffset The first vertex of this part within the buffer.
         * @param numVertices How many vertices this part uses.
         * @param startIndex The first index of this part within the index buffer.
         * @param primitiveCount How many triangles this part draws.
         */
        CNAEXT ModelMeshPartContent(std::shared_ptr<VertexBufferContent> vertexBuffer,
                                    std::shared_ptr<Graphics::IndexCollection> indexBuffer,
                                    SharpRuntime::intcs vertexOffset, SharpRuntime::intcs numVertices,
                                    SharpRuntime::intcs startIndex, SharpRuntime::intcs primitiveCount);

        /**
         * @brief Gets the indices this part draws with.
         * @return The index buffer.
         */
        [[nodiscard]] const std::shared_ptr<Graphics::IndexCollection>& getIndexBufferProperty() const noexcept;

        /**
         * @brief Gets the material this part is drawn with.
         * @return The material, or null when it has none.
         */
        [[nodiscard]] const std::shared_ptr<Graphics::MaterialContent>& getMaterialProperty() const noexcept;

        /**
         * @brief Sets the material this part is drawn with.
         * @param value The material.
         */
        void setMaterialProperty(std::shared_ptr<Graphics::MaterialContent> value) noexcept;

        /**
         * @brief Gets how many vertices this part uses.
         * @return The vertex count.
         */
        [[nodiscard]] SharpRuntime::intcs getNumVerticesProperty() const noexcept;

        /**
         * @brief Gets how many triangles this part draws.
         * @return The primitive count.
         */
        [[nodiscard]] SharpRuntime::intcs getPrimitiveCountProperty() const noexcept;

        /**
         * @brief Gets the first index of this part within the index buffer.
         * @return The start index.
         */
        [[nodiscard]] SharpRuntime::intcs getStartIndexProperty() const noexcept;

        /**
         * @brief Gets the object a game attached to this part.
         * @return The tag, or an empty box.
         */
        [[nodiscard]] const ContentObject& getTagProperty() const noexcept;

        /**
         * @brief Sets the object a game attaches to this part.
         * @param value The tag.
         */
        void setTagProperty(ContentObject value) noexcept;

        /**
         * @brief Gets the vertices this part draws from.
         * @return The vertex buffer.
         */
        [[nodiscard]] const std::shared_ptr<VertexBufferContent>& getVertexBufferProperty() const noexcept;

        /**
         * @brief Gets the first vertex of this part within the buffer.
         * @return The vertex offset.
         */
        [[nodiscard]] SharpRuntime::intcs getVertexOffsetProperty() const noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::shared_ptr<VertexBufferContent> vertexBuffer_;
        std::shared_ptr<Graphics::IndexCollection> indexBuffer_;
        std::shared_ptr<Graphics::MaterialContent> material_;
        SharpRuntime::intcs vertexOffset_ = 0;
        SharpRuntime::intcs numVertices_ = 0;
        SharpRuntime::intcs startIndex_ = 0;
        SharpRuntime::intcs primitiveCount_ = 0;
        ContentObject tag_;
    };

    /** @brief A read-only list of mesh parts, as XNA's ReadOnlyCollection is. */
    using ModelMeshPartContentCollection = std::vector<std::shared_ptr<ModelMeshPartContent>>;

    /**
     * @brief One processed mesh: its parts, the bone it hangs from and the sphere that bounds it.
     */
    class ModelMeshContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelMeshContent";

        /**
         * @brief Initializes a mesh; only the model processor does.
         *
         * @param name The mesh's name.
         * @param sourceMesh The mesh it was processed from.
         * @param parentBone The bone it hangs from.
         * @param boundingSphere The sphere bounding its vertices.
         * @param meshParts Its drawable parts.
         */
        CNAEXT ModelMeshContent(std::string name, std::shared_ptr<Graphics::MeshContent> sourceMesh,
                                std::shared_ptr<ModelBoneContent> parentBone, BoundingSphere boundingSphere,
                                ModelMeshPartContentCollection meshParts);

        /**
         * @brief Gets the sphere bounding this mesh's vertices.
         * @return The bounding sphere.
         */
        [[nodiscard]] const BoundingSphere& getBoundingSphereProperty() const noexcept;

        /**
         * @brief Gets the drawable parts of this mesh.
         * @return The parts.
         */
        [[nodiscard]] const ModelMeshPartContentCollection& getMeshPartsProperty() const noexcept;

        /**
         * @brief Gets the mesh's name.
         * @return The name.
         */
        [[nodiscard]] const std::string& getNameProperty() const noexcept;

        /**
         * @brief Gets the bone this mesh hangs from.
         * @return The parent bone.
         */
        [[nodiscard]] const std::shared_ptr<ModelBoneContent>& getParentBoneProperty() const noexcept;

        /**
         * @brief Gets the mesh this one was processed from.
         * @return The source mesh.
         */
        [[nodiscard]] const std::shared_ptr<Graphics::MeshContent>& getSourceMeshProperty() const noexcept;

        /**
         * @brief Gets the object a game attached to this mesh.
         * @return The tag, or an empty box.
         */
        [[nodiscard]] const ContentObject& getTagProperty() const noexcept;

        /**
         * @brief Sets the object a game attaches to this mesh.
         * @param value The tag.
         */
        void setTagProperty(ContentObject value) noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string name_;
        std::shared_ptr<Graphics::MeshContent> sourceMesh_;
        std::shared_ptr<ModelBoneContent> parentBone_;
        BoundingSphere boundingSphere_;
        ModelMeshPartContentCollection meshParts_;
        ContentObject tag_;
    };

    /** @brief A read-only list of meshes, as XNA's ReadOnlyCollection is. */
    using ModelMeshContentCollection = std::vector<std::shared_ptr<ModelMeshContent>>;

    /**
     * @brief A processed model: its skeleton, its meshes and the root they hang from.
     */
    class ModelContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelContent";

        /**
         * @brief Initializes a model; only the model processor does.
         *
         * @param root The root bone.
         * @param bones Every bone, in index order.
         * @param meshes Every mesh.
         */
        CNAEXT ModelContent(std::shared_ptr<ModelBoneContent> root, ModelBoneContentCollection bones,
                            ModelMeshContentCollection meshes);

        /**
         * @brief Gets every bone of the model, in index order.
         * @return The bones.
         */
        [[nodiscard]] const ModelBoneContentCollection& getBonesProperty() const noexcept;

        /**
         * @brief Gets every mesh of the model.
         * @return The meshes.
         */
        [[nodiscard]] const ModelMeshContentCollection& getMeshesProperty() const noexcept;

        /**
         * @brief Gets the bone every other hangs from.
         * @return The root bone.
         */
        [[nodiscard]] const std::shared_ptr<ModelBoneContent>& getRootProperty() const noexcept;

        /**
         * @brief Gets the object a game attached to this model.
         * @return The tag, or an empty box.
         */
        [[nodiscard]] const ContentObject& getTagProperty() const noexcept;

        /**
         * @brief Sets the object a game attaches to this model.
         * @param value The tag.
         */
        void setTagProperty(ContentObject value) noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::shared_ptr<ModelBoneContent> root_;
        ModelBoneContentCollection bones_;
        ModelMeshContentCollection meshes_;
        ContentObject tag_;
    };
}
