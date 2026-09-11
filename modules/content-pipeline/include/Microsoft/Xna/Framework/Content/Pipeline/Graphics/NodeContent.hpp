// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ChildCollection.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MaterialContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexCollections.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    class NodeContent;
    class MeshContent;
    class GeometryContent;

    /**
     * @brief Collection of the child nodes of one node, which owns them.
     */
    class NodeContentCollection final : public ChildCollection<NodeContent, NodeContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContentCollection";

        /** @brief The element each child is written as. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Child";

        /**
         * @brief Initializes a collection belonging to a node.
         *
         * @param parent The node whose children these are.
         */
        CNAEXT explicit NodeContentCollection(NodeContent* parent);

        /** @brief Initializes a parentless collection, which only the serializer needs. */
        CNAEXT NodeContentCollection();

    protected:
        /**
         * @brief Gets the parent of a child node.
         *
         * @param child The child.
         * @return The child's parent.
         */
        [[nodiscard]] NodeContent* GetParent(const std::shared_ptr<NodeContent>& child) const override;

        /**
         * @brief Sets the parent of a child node.
         *
         * @param child The child.
         * @param parent The new parent.
         */
        void SetParent(const std::shared_ptr<NodeContent>& child, NodeContent* parent) override;
    };

    /**
     * @brief Provides a base class for graphics types that define local coordinate systems.
     */
    class NodeContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent";

        /** @brief Initializes a node with an identity transform and no children. */
        NodeContent();

        /** @brief A node owns its children, so it is not copied. */
        NodeContent(const NodeContent&) = delete;

        /** @brief A node owns its children, so it is not copied. */
        NodeContent& operator=(const NodeContent&) = delete;

        /** @brief Destroys the node. */
        ~NodeContent() override = default;

        /**
         * @brief Gets the transform of this node combined with every transform above it.
         *
         * @return The absolute transform.
         */
        [[nodiscard]] Matrix getAbsoluteTransformProperty() const;

        /**
         * @brief Gets the animations belonging to this node.
         *
         * @return The animations, keyed by name.
         */
        [[nodiscard]] AnimationContentDictionary& getAnimationsProperty() noexcept;

        /**
         * @brief Gets the animations belonging to this node.
         *
         * @return The animations, keyed by name.
         */
        [[nodiscard]] const AnimationContentDictionary& getAnimationsProperty() const noexcept;

        /**
         * @brief Gets the children of this node.
         *
         * @return The children.
         */
        [[nodiscard]] NodeContentCollection& getChildrenProperty() noexcept;

        /**
         * @brief Gets the children of this node.
         *
         * @return The children.
         */
        [[nodiscard]] const NodeContentCollection& getChildrenProperty() const noexcept;

        /**
         * @brief Gets the node this one hangs from.
         *
         * @return The parent, or null for a root node.
         */
        [[nodiscard]] NodeContent* getParentProperty() const noexcept;

        /**
         * @brief Gets the transform of this node relative to its parent.
         *
         * @return The transform.
         */
        [[nodiscard]] const Matrix& getTransformProperty() const noexcept;

        /**
         * @brief Sets the transform of this node relative to its parent.
         *
         * @param value The transform.
         */
        void setTransformProperty(Matrix value) noexcept;

        /**
         * @brief Describes the node for the intermediate serializer: ContentItem's members, the
         *        transform, then the children and animations, each written only when it has any.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<NodeContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this node's type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        friend class NodeContentCollection;

        Matrix transform_;
        NodeContent* parent_ = nullptr;
        NodeContentCollection children_;
        AnimationContentDictionary animations_;
    };

    /**
     * @brief Represents a bone in a skeleton: a node with nothing of its own.
     */
    class BoneContent : public NodeContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.BoneContent";

        /** @brief Initializes a bone. */
        BoneContent() = default;

        /**
         * @brief Describes the bone for the intermediate serializer: exactly a node's members.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<BoneContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Collection of the geometry batches of one mesh, which owns them.
     */
    class GeometryContentCollection final : public ChildCollection<MeshContent, GeometryContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContentCollection";

        /** @brief The element each batch is written as. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Batch";

        /**
         * @brief Initializes a collection belonging to a mesh.
         *
         * @param parent The mesh whose batches these are.
         */
        CNAEXT explicit GeometryContentCollection(MeshContent* parent);

        /** @brief Initializes a parentless collection, which only the serializer needs. */
        CNAEXT GeometryContentCollection();

    protected:
        /**
         * @brief Gets the mesh a batch belongs to.
         *
         * @param child The batch.
         * @return The batch's mesh.
         */
        [[nodiscard]] MeshContent* GetParent(const std::shared_ptr<GeometryContent>& child) const override;

        /**
         * @brief Sets the mesh a batch belongs to.
         *
         * @param child The batch.
         * @param parent The new mesh.
         */
        void SetParent(const std::shared_ptr<GeometryContent>& child, MeshContent* parent) override;
    };

    /**
     * @brief Provides properties for maintaining a mesh: its positions and its geometry batches.
     */
    class MeshContent : public NodeContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent";

        /** @brief Initializes a mesh with no positions and no geometry. */
        MeshContent();

        /**
         * @brief Gets the geometry batches of this mesh.
         *
         * @return The batches.
         */
        [[nodiscard]] GeometryContentCollection& getGeometryProperty() noexcept;

        /**
         * @brief Gets the geometry batches of this mesh.
         *
         * @return The batches.
         */
        [[nodiscard]] const GeometryContentCollection& getGeometryProperty() const noexcept;

        /**
         * @brief Gets the positions the batches index into.
         *
         * @return The positions.
         */
        [[nodiscard]] PositionCollection& getPositionsProperty() noexcept;

        /**
         * @brief Gets the positions the batches index into.
         *
         * @return The positions.
         */
        [[nodiscard]] const PositionCollection& getPositionsProperty() const noexcept;

        /**
         * @brief Describes the mesh for the intermediate serializer: a node's members, then the
         *        positions and the geometry batches.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<MeshContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        GeometryContentCollection geometry_;
        PositionCollection positions_;
    };

    /**
     * @brief Provides properties for maintaining a batch of indexed geometry: its vertices, its
     *        triangle indices and the material they are drawn with.
     */
    class GeometryContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent";

        /** @brief Initializes a batch with no vertices, no indices and no material. */
        GeometryContent();

        /** @brief A batch owns its vertices, so it is not copied. */
        GeometryContent(const GeometryContent&) = delete;

        /** @brief A batch owns its vertices, so it is not copied. */
        GeometryContent& operator=(const GeometryContent&) = delete;

        /** @brief Destroys the batch. */
        ~GeometryContent() override = default;

        /**
         * @brief Gets the triangle indices, three per triangle, into this batch's vertices.
         *
         * @return The indices.
         */
        [[nodiscard]] IndexCollection& getIndicesProperty() noexcept;

        /**
         * @brief Gets the triangle indices, three per triangle, into this batch's vertices.
         *
         * @return The indices.
         */
        [[nodiscard]] const IndexCollection& getIndicesProperty() const noexcept;

        /**
         * @brief Gets the material this batch is drawn with.
         *
         * @return The material, or null when it has none.
         */
        [[nodiscard]] const std::shared_ptr<MaterialContent>& getMaterialProperty() const noexcept;

        /**
         * @brief Sets the material this batch is drawn with.
         *
         * @param value The material, or null for none.
         */
        void setMaterialProperty(std::shared_ptr<MaterialContent> value) noexcept;

        /**
         * @brief Gets the mesh this batch belongs to.
         *
         * @return The mesh, or null while the batch belongs to none.
         */
        [[nodiscard]] MeshContent* getParentProperty() const noexcept;

        /**
         * @brief Gets the vertices of this batch.
         *
         * @return The vertices.
         */
        [[nodiscard]] VertexContent& getVerticesProperty() noexcept;

        /**
         * @brief Gets the vertices of this batch.
         *
         * @return The vertices.
         */
        [[nodiscard]] const VertexContent& getVerticesProperty() const noexcept;

        /**
         * @brief Describes the batch for the intermediate serializer: ContentItem's members, the
         *        material as a shared resource, the indices, then the vertices.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<GeometryContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        friend class GeometryContentCollection;

        MeshContent* parent_ = nullptr;
        IndexCollection indices_;
        std::shared_ptr<MaterialContent> material_;
        std::shared_ptr<VertexContent> vertices_;
    };
}
