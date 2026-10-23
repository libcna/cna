// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/NotSupportedException.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::GamerServices
{
    AchievementCollection::AchievementCollection(std::vector<Achievement> achievements)
        : collection_(std::move(achievements))
    {
    }

    AchievementCollection AchievementCollection::CreateInternal(std::vector<Achievement> achievements)
    {
        return AchievementCollection(std::move(achievements));
    }

    int AchievementCollection::getCountProperty() const     { return static_cast<int>(collection_.size()); }
    bool AchievementCollection::getIsDisposedProperty() const { return isDisposed_; }

    const Achievement& AchievementCollection::operator[](int index) const
    {
        // Task 7.9: FNA's own int indexer (`return collection[index];`, a List<T>) throws
        // ArgumentOutOfRangeException, not std::out_of_range - use ThrowIfNegative/
        // ThrowIfGreaterThanOrEqual for the matching sharp-runtime exception type instead of
        // relying on std::vector::at()'s own (differently-typed) exception.
        System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
        System::ArgumentOutOfRangeException::ThrowIfGreaterThanOrEqual(
            index, static_cast<int>(collection_.size()), "index"
        );
        return collection_[static_cast<std::size_t>(index)];
    }

    const Achievement& AchievementCollection::operator[](const std::string& achievementKey) const
    {
        for (const auto& ach : collection_)
        {
            if (ach.getKeyProperty() == achievementKey)
                return ach;
        }
        // XNA's dictionary lookup differs from FNA's IndexOutOfRangeException.
        throw System::Collections::Generic::KeyNotFoundException();
    }

    void AchievementCollection::Dispose()
    {
        if (!isDisposed_)
        {
            collection_.clear();
            isDisposed_ = true;
        }
    }

    int AchievementCollection::IndexOf(const Achievement& item) const
    {
        for (std::size_t i = 0; i < collection_.size(); ++i)
        {
            if (collection_[i] == item)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void AchievementCollection::Insert(int, const Achievement&)
    {
        // XNA refuses IList/ICollection mutation before checking arguments;
        // FNA's writable implementation contradicts this read-only contract.
        throw System::NotSupportedException();
    }

    void AchievementCollection::RemoveAt(int)
    {
        throw System::NotSupportedException();
    }

    void AchievementCollection::Add(const Achievement&)
    {
        throw System::NotSupportedException();
    }

    bool AchievementCollection::Remove(const Achievement&)
    {
        throw System::NotSupportedException();
    }

    void AchievementCollection::Clear()
    {
        throw System::NotSupportedException();
    }

    bool AchievementCollection::Contains(const Achievement& item) const
    {
        return IndexOf(item) >= 0;
    }

    void AchievementCollection::CopyTo(std::vector<Achievement>& array, int arrayIndex) const
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(arrayIndex, "arrayIndex");
        if (static_cast<int>(array.size()) - arrayIndex < static_cast<int>(collection_.size()))
        {
            throw System::ArgumentException("The number of elements in the source collection is "
                                             "greater than the available space from arrayIndex to "
                                             "the end of the destination array.");
        }
        std::copy(collection_.begin(), collection_.end(), array.begin() + arrayIndex);
    }

    bool AchievementCollection::getIsReadOnlyProperty() const
    {
        return true;
    }
}
