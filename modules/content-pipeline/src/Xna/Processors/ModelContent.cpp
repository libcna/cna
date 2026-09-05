// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelContent.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    // ------------------------------------------------------------------------------------------
    // ModelBoneContent
    // ------------------------------------------------------------------------------------------

    ModelBoneContent::ModelBoneContent(std::string name, SharpRuntime::intcs index, Matrix transform,
                                       std::shared_ptr<ModelBoneContent> parent)
        : name_(std::move(name)), index_(index), transform_(transform), parent_(parent)
    {
    }

    const ModelBoneContentCollection& ModelBoneContent::getChildrenProperty() const noexcept { return children_; }

    SharpRuntime::intcs ModelBoneContent::getIndexProperty() const noexcept { return index_; }

    const std::string& ModelBoneContent::getNameProperty() const noexcept { return name_; }

    std::shared_ptr<ModelBoneContent> ModelBoneContent::getParentProperty() const noexcept { return parent_.lock(); }

    const Matrix& ModelBoneContent::getTransformProperty() const noexcept { return transform_; }

    void ModelBoneContent::setTransformProperty(Matrix value) noexcept { transform_ = value; }

    void ModelBoneContent::AddChild(std::shared_ptr<ModelBoneContent> child)
    {
        children_.push_back(std::move(child));
    }

    const std::string& ModelBoneContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // ModelMeshPartContent
    // ------------------------------------------------------------------------------------------

    ModelMeshPartContent::ModelMeshPartContent(std::shared_ptr<VertexBufferContent> vertexBuffer,
                                               std::shared_ptr<Graphics::IndexCollection> indexBuffer,
                                               SharpRuntime::intcs vertexOffset, SharpRuntime::intcs numVertices,
                                               SharpRuntime::intcs startIndex, SharpRuntime::intcs primitiveCount)
        : vertexBuffer_(std::move(vertexBuffer)), indexBuffer_(std::move(indexBuffer)), vertexOffset_(vertexOffset),
          numVertices_(numVertices), startIndex_(startIndex), primitiveCount_(primitiveCount)
    {
    }

    const std::shared_ptr<Graphics::IndexCollection>& ModelMeshPartContent::getIndexBufferProperty() const noexcept
    {
        return indexBuffer_;
    }

    const std::shared_ptr<Graphics::MaterialContent>& ModelMeshPartContent::getMaterialProperty() const noexcept
    {
        return material_;
    }

    void ModelMeshPartContent::setMaterialProperty(std::shared_ptr<Graphics::MaterialContent> value) noexcept
    {
        material_ = std::move(value);
    }

    SharpRuntime::intcs ModelMeshPartContent::getNumVerticesProperty() const noexcept { return numVertices_; }

    SharpRuntime::intcs ModelMeshPartContent::getPrimitiveCountProperty() const noexcept { return primitiveCount_; }

    SharpRuntime::intcs ModelMeshPartContent::getStartIndexProperty() const noexcept { return startIndex_; }

    const ContentObject& ModelMeshPartContent::getTagProperty() const noexcept { return tag_; }

    void ModelMeshPartContent::setTagProperty(ContentObject value) noexcept { tag_ = std::move(value); }

    const std::shared_ptr<VertexBufferContent>& ModelMeshPartContent::getVertexBufferProperty() const noexcept
    {
        return vertexBuffer_;
    }

    SharpRuntime::intcs ModelMeshPartContent::getVertexOffsetProperty() const noexcept { return vertexOffset_; }

    const std::string& ModelMeshPartContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // ModelMeshContent
    // ------------------------------------------------------------------------------------------

    ModelMeshContent::ModelMeshContent(std::string name, std::shared_ptr<Graphics::MeshContent> sourceMesh,
                                       std::shared_ptr<ModelBoneContent> parentBone, BoundingSphere boundingSphere,
                                       ModelMeshPartContentCollection meshParts)
        : name_(std::move(name)), sourceMesh_(std::move(sourceMesh)), parentBone_(std::move(parentBone)),
          boundingSphere_(boundingSphere), meshParts_(std::move(meshParts))
    {
    }

    const BoundingSphere& ModelMeshContent::getBoundingSphereProperty() const noexcept { return boundingSphere_; }

    const ModelMeshPartContentCollection& ModelMeshContent::getMeshPartsProperty() const noexcept
    {
        return meshParts_;
    }

    const std::string& ModelMeshContent::getNameProperty() const noexcept { return name_; }

    const std::shared_ptr<ModelBoneContent>& ModelMeshContent::getParentBoneProperty() const noexcept
    {
        return parentBone_;
    }

    const std::shared_ptr<Graphics::MeshContent>& ModelMeshContent::getSourceMeshProperty() const noexcept
    {
        return sourceMesh_;
    }

    const ContentObject& ModelMeshContent::getTagProperty() const noexcept { return tag_; }

    void ModelMeshContent::setTagProperty(ContentObject value) noexcept { tag_ = std::move(value); }

    const std::string& ModelMeshContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // ModelContent
    // ------------------------------------------------------------------------------------------

    ModelContent::ModelContent(std::shared_ptr<ModelBoneContent> root, ModelBoneContentCollection bones,
                               ModelMeshContentCollection meshes)
        : root_(std::move(root)), bones_(std::move(bones)), meshes_(std::move(meshes))
    {
    }

    const ModelBoneContentCollection& ModelContent::getBonesProperty() const noexcept { return bones_; }

    const ModelMeshContentCollection& ModelContent::getMeshesProperty() const noexcept { return meshes_; }

    const std::shared_ptr<ModelBoneContent>& ModelContent::getRootProperty() const noexcept { return root_; }

    const ContentObject& ModelContent::getTagProperty() const noexcept { return tag_; }

    void ModelContent::setTagProperty(ContentObject value) noexcept { tag_ = std::move(value); }

    const std::string& ModelContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
