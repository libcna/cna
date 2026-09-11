// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"

#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    // ------------------------------------------------------------------------------------------
    // NodeContentCollection
    // ------------------------------------------------------------------------------------------

    NodeContentCollection::NodeContentCollection(NodeContent* parent) : ChildCollection(parent) {}

    NodeContentCollection::NodeContentCollection() : ChildCollection(NoParentTag{}) {}

    NodeContent* NodeContentCollection::GetParent(const std::shared_ptr<NodeContent>& child) const
    {
        return child == nullptr ? nullptr : child->parent_;
    }

    void NodeContentCollection::SetParent(const std::shared_ptr<NodeContent>& child, NodeContent* parent)
    {
        if (child != nullptr)
        {
            child->parent_ = parent;
        }
    }

    // ------------------------------------------------------------------------------------------
    // NodeContent
    // ------------------------------------------------------------------------------------------

    NodeContent::NodeContent() : transform_(Matrix::getIdentityProperty()), children_(this) {}

    Matrix NodeContent::getAbsoluteTransformProperty() const
    {
        // The transform of this node combined with every transform above it (measured,
        // tests/reference/xna40/graphics case node/absolute_transform).
        if (parent_ == nullptr)
        {
            return transform_;
        }
        return transform_ * parent_->getAbsoluteTransformProperty();
    }

    AnimationContentDictionary& NodeContent::getAnimationsProperty() noexcept { return animations_; }

    const AnimationContentDictionary& NodeContent::getAnimationsProperty() const noexcept { return animations_; }

    NodeContentCollection& NodeContent::getChildrenProperty() noexcept { return children_; }

    const NodeContentCollection& NodeContent::getChildrenProperty() const noexcept { return children_; }

    NodeContent* NodeContent::getParentProperty() const noexcept { return parent_; }

    const Matrix& NodeContent::getTransformProperty() const noexcept { return transform_; }

    void NodeContent::setTransformProperty(Matrix value) noexcept { transform_ = value; }

    void NodeContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<NodeContent>& d)
    {
        // Name and OpaqueData, then the transform, then the two collections -- each of which is
        // written only when it has anything (measured, node/serialize and
        // node/serialize_with_animation).
        d.BaseType<ContentItem>();
        d.Property("Transform", &NodeContent::getTransformProperty, &NodeContent::setTransformProperty);
        d.ReadOnlyProperty("Children", [](NodeContent& node) -> NodeContentCollection&
                           { return node.getChildrenProperty(); })
            .Optional()
            .CollectionItemName(std::string(NodeContentCollection::CollectionItemName));
        d.ReadOnlyProperty("Animations", [](NodeContent& node) -> AnimationContentDictionary&
                           { return node.getAnimationsProperty(); })
            .Optional();
    }

    const std::string& NodeContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string NodeContent::ToString() const { return GetTypeName(); }

    // ------------------------------------------------------------------------------------------
    // BoneContent
    // ------------------------------------------------------------------------------------------

    void BoneContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<BoneContent>& d)
    {
        d.BaseType<NodeContent>();
    }

    const std::string& BoneContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // GeometryContentCollection
    // ------------------------------------------------------------------------------------------

    GeometryContentCollection::GeometryContentCollection(MeshContent* parent) : ChildCollection(parent) {}

    GeometryContentCollection::GeometryContentCollection() : ChildCollection(NoParentTag{}) {}

    MeshContent* GeometryContentCollection::GetParent(const std::shared_ptr<GeometryContent>& child) const
    {
        return child == nullptr ? nullptr : child->parent_;
    }

    void GeometryContentCollection::SetParent(const std::shared_ptr<GeometryContent>& child, MeshContent* parent)
    {
        if (child != nullptr)
        {
            child->parent_ = parent;
        }
    }

    // ------------------------------------------------------------------------------------------
    // MeshContent
    // ------------------------------------------------------------------------------------------

    MeshContent::MeshContent() : geometry_(this) {}

    GeometryContentCollection& MeshContent::getGeometryProperty() noexcept { return geometry_; }

    const GeometryContentCollection& MeshContent::getGeometryProperty() const noexcept { return geometry_; }

    PositionCollection& MeshContent::getPositionsProperty() noexcept { return positions_; }

    const PositionCollection& MeshContent::getPositionsProperty() const noexcept { return positions_; }

    void MeshContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<MeshContent>& d)
    {
        d.BaseType<NodeContent>();
        d.ReadOnlyProperty("Positions", [](MeshContent& mesh) -> PositionCollection&
                           { return mesh.getPositionsProperty(); });
        d.ReadOnlyProperty("Geometry", [](MeshContent& mesh) -> GeometryContentCollection&
                           { return mesh.getGeometryProperty(); })
            .CollectionItemName(std::string(GeometryContentCollection::CollectionItemName));
    }

    const std::string& MeshContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // GeometryContent
    // ------------------------------------------------------------------------------------------

    GeometryContent::GeometryContent() : vertices_(std::make_shared<VertexContent>(this)) {}

    IndexCollection& GeometryContent::getIndicesProperty() noexcept { return indices_; }

    const IndexCollection& GeometryContent::getIndicesProperty() const noexcept { return indices_; }

    const std::shared_ptr<MaterialContent>& GeometryContent::getMaterialProperty() const noexcept
    {
        return material_;
    }

    void GeometryContent::setMaterialProperty(std::shared_ptr<MaterialContent> value) noexcept
    {
        material_ = std::move(value);
    }

    MeshContent* GeometryContent::getParentProperty() const noexcept { return parent_; }

    VertexContent& GeometryContent::getVerticesProperty() noexcept { return *vertices_; }

    const VertexContent& GeometryContent::getVerticesProperty() const noexcept { return *vertices_; }

    void GeometryContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<GeometryContent>& d)
    {
        // The material is a shared resource, written once under <Resources> however many batches
        // name it (measured, mesh/serialize).
        d.BaseType<ContentItem>();
        d.Property("Material", &GeometryContent::getMaterialProperty, &GeometryContent::setMaterialProperty)
            .SharedResource()
            .Optional();
        d.ReadOnlyProperty("Indices", [](GeometryContent& geometry) -> IndexCollection&
                           { return geometry.getIndicesProperty(); });
        d.ReadOnlyProperty("Vertices", [](GeometryContent& geometry) -> std::shared_ptr<VertexContent>&
                           { return geometry.vertices_; });
    }

    const std::string& GeometryContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string GeometryContent::ToString() const { return GetTypeName(); }
}
