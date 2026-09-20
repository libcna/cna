// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"

#include <algorithm>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelEffectCollection::Enumerator::Enumerator(const ModelEffectCollection& collection)
        : collection_(&collection), position_(-1), version_(collection.version_)
    {
    }

    Effect* const& ModelEffectCollection::Enumerator::Current() const
    {
        return current_;
    }

    std::any ModelEffectCollection::Enumerator::getCurrentProperty() const
    {
        if (!hasCurrent_)
        {
            throw System::InvalidOperationException("Enumeration has not started or has finished.");
        }
        return std::any(current_);
    }

    bool ModelEffectCollection::Enumerator::MoveNext()
    {
        if (collection_ == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (version_ != collection_->version_)
        {
            throw System::InvalidOperationException("Collection was modified during enumeration.");
        }
        ++position_;
        if (position_ >= static_cast<int>(collection_->effects_.size()))
        {
            position_ = static_cast<int>(collection_->effects_.size());
            current_ = nullptr;
            hasCurrent_ = false;
            return false;
        }
        current_ = collection_->effects_[static_cast<std::size_t>(position_)];
        hasCurrent_ = true;
        return true;
    }

    void ModelEffectCollection::Enumerator::Reset()
    {
        if (collection_ == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (version_ != collection_->version_)
        {
            throw System::InvalidOperationException("Collection was modified during enumeration.");
        }
        position_ = -1;
        current_ = nullptr;
        hasCurrent_ = false;
    }

    void ModelEffectCollection::Enumerator::Dispose()
    {
    }

    ModelEffectCollection::Enumerator ModelEffectCollection::GetEnumerator() const
    {
        return Enumerator(*this);
    }

    Effect* ModelEffectCollection::operator[](int index) const
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
        System::ArgumentOutOfRangeException::ThrowIfGreaterThanOrEqual(
            index, static_cast<int>(effects_.size()), "index"
        );
        return effects_[static_cast<std::size_t>(index)];
    }

    int ModelEffectCollection::getCountProperty() const
    {
        return static_cast<int>(effects_.size());
    }

    bool ModelEffectCollection::Contains(const Effect* effect) const
    {
        return std::find(effects_.begin(), effects_.end(), effect) != effects_.end();
    }

    void ModelEffectCollection::Add(Effect* effect)
    {
        effects_.push_back(effect);
        ++version_;
    }

    void ModelEffectCollection::Remove(const Effect* effect)
    {
        auto it = std::find(effects_.begin(), effects_.end(), effect);
        if (it != effects_.end())
        {
            effects_.erase(it);
            ++version_;
        }
    }

    ModelEffectCollection::iterator ModelEffectCollection::begin() { return effects_.begin(); }
    ModelEffectCollection::iterator ModelEffectCollection::end()   { return effects_.end(); }
    ModelEffectCollection::const_iterator ModelEffectCollection::begin() const { return effects_.begin(); }
    ModelEffectCollection::const_iterator ModelEffectCollection::end()   const { return effects_.end(); }
}
