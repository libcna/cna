// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "System/NullReferenceException.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMeshPartCollection::Enumerator::Enumerator(const ModelMeshPartCollection& collection)
        : collection_(&collection), position_(-1),
          count_(static_cast<int>(collection.parts_.size()))
    {
    }

    ModelMeshPart* const& ModelMeshPartCollection::Enumerator::Current() const
    {
        if (collection_ == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (position_ < 0 || position_ >= count_ ||
            position_ >= static_cast<int>(collection_->parts_.size()))
        {
            throw System::IndexOutOfRangeException();
        }
        return collection_->parts_[static_cast<std::size_t>(position_)];
    }

    bool ModelMeshPartCollection::Enumerator::MoveNext()
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

    void ModelMeshPartCollection::Enumerator::Reset()
    {
        position_ = -1;
    }

    void ModelMeshPartCollection::Enumerator::Dispose()
    {
    }

    ModelMeshPartCollection::Enumerator ModelMeshPartCollection::GetEnumerator() const
    {
        return Enumerator(*this);
    }

    ModelMeshPart* ModelMeshPartCollection::operator[](int index) const
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
        System::ArgumentOutOfRangeException::ThrowIfGreaterThanOrEqual(
            index, static_cast<int>(parts_.size()), "index"
        );
        return parts_[static_cast<std::size_t>(index)];
    }

    int ModelMeshPartCollection::getCountProperty() const
    {
        return static_cast<int>(parts_.size());
    }

    ModelMeshPartCollection::iterator ModelMeshPartCollection::begin() { return parts_.begin(); }
    ModelMeshPartCollection::iterator ModelMeshPartCollection::end()   { return parts_.end(); }
    ModelMeshPartCollection::const_iterator ModelMeshPartCollection::begin() const { return parts_.begin(); }
    ModelMeshPartCollection::const_iterator ModelMeshPartCollection::end()   const { return parts_.end(); }
}
