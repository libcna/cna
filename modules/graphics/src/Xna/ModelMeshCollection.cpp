// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "System/NullReferenceException.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMeshCollection::Enumerator::Enumerator(const ModelMeshCollection& collection)
        : collection_(&collection), position_(-1),
          count_(static_cast<int>(collection.meshes_.size()))
    {
    }

    ModelMesh* const& ModelMeshCollection::Enumerator::Current() const
    {
        if (collection_ == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (position_ < 0 || position_ >= count_ ||
            position_ >= static_cast<int>(collection_->meshes_.size()))
        {
            throw System::IndexOutOfRangeException();
        }
        return collection_->meshes_[static_cast<std::size_t>(position_)];
    }

    bool ModelMeshCollection::Enumerator::MoveNext()
    {
        if (collection_ == nullptr)
        {
            throw System::NullReferenceException();
        }
        ++position_;
        if (position_ >= count_)
        {
            position_ = count_;
            return false;
        }
        return true;
    }

    void ModelMeshCollection::Enumerator::Reset()
    {
        position_ = -1;
    }

    void ModelMeshCollection::Enumerator::Dispose()
    {
    }

    ModelMeshCollection::Enumerator ModelMeshCollection::GetEnumerator() const
    {
        return Enumerator(*this);
    }

    ModelMesh* ModelMeshCollection::operator[](int index) const
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
        System::ArgumentOutOfRangeException::ThrowIfGreaterThanOrEqual(
            index, static_cast<int>(meshes_.size()), "index"
        );
        return meshes_[static_cast<std::size_t>(index)];
    }

    ModelMesh* ModelMeshCollection::operator[](const std::string& name) const
    {
        ModelMesh* value = nullptr;
        if (TryGetValue(name, value))
            return value;
        throw System::Collections::Generic::KeyNotFoundException();
    }

    int ModelMeshCollection::getCountProperty() const
    {
        return static_cast<int>(meshes_.size());
    }

    bool ModelMeshCollection::TryGetValue(const std::string& meshName, ModelMesh*& value) const
    {
        if (meshName.empty())
            throw System::ArgumentNullException("meshName");

        for (ModelMesh* mesh : meshes_)
        {
            if (mesh && mesh->getNameProperty() == meshName)
            {
                value = mesh;
                return true;
            }
        }
        value = nullptr;
        return false;
    }

    bool ModelMeshCollection::Contains(ModelMesh* item) const
    {
        for (ModelMesh* mesh : meshes_)
        {
            if (mesh == item)
                return true;
        }
        return false;
    }

    ModelMeshCollection::iterator ModelMeshCollection::begin() { return meshes_.begin(); }
    ModelMeshCollection::iterator ModelMeshCollection::end()   { return meshes_.end(); }
    ModelMeshCollection::const_iterator ModelMeshCollection::begin() const { return meshes_.begin(); }
    ModelMeshCollection::const_iterator ModelMeshCollection::end()   const { return meshes_.end(); }
}
