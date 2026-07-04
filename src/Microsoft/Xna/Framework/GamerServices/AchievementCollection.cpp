// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"

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
        return collection_.at(static_cast<std::size_t>(index));
    }

    const Achievement& AchievementCollection::operator[](const std::string& achievementKey) const
    {
        for (const auto& ach : collection_)
        {
            if (ach.getKeyProperty() == achievementKey)
                return ach;
        }
        throw std::out_of_range("Achievement key not found: " + achievementKey);
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
