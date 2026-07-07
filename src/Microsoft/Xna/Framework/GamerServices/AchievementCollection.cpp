// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IndexOutOfRangeException.hpp"

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
        // Task 7.9: FNA's own string-key indexer explicitly does `throw new
        // IndexOutOfRangeException();` - not std::out_of_range.
        throw System::IndexOutOfRangeException();
    }

    void AchievementCollection::Dispose()
    {
        if (!isDisposed_)
        {
            collection_.clear();
            isDisposed_ = true;
        }
    }
}
