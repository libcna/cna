// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Net
{
    int NetworkSessionProperties::getCountProperty() const
    {
        return static_cast<int>(properties_.size());
    }

    const std::optional<int>& NetworkSessionProperties::operator[](int index) const
    {
        return properties_.at(static_cast<std::size_t>(index));
    }

    std::optional<int>& NetworkSessionProperties::operator[](int index)
    {
        // Matches FNA exactly: assigning past the end appends instead of extending out to
        // `index` (the reference source itself has a "TODO: Expand list to index size?" comment).
        if (index >= static_cast<int>(properties_.size()))
        {
            properties_.push_back(std::nullopt);
            return properties_.back();
        }
        return properties_[static_cast<std::size_t>(index)];
    }

    int NetworkSessionProperties::IndexOf(const std::optional<int>& item) const
    {
        auto it = std::find(properties_.begin(), properties_.end(), item);
        if (it == properties_.end())
            return -1;
        return static_cast<int>(std::distance(properties_.begin(), it));
    }

    void NetworkSessionProperties::Insert(int index, const std::optional<int>& item)
    {
        properties_.insert(properties_.begin() + index, item);
    }

    void NetworkSessionProperties::RemoveAt(int index)
    {
        properties_.erase(properties_.begin() + index);
    }

    bool NetworkSessionProperties::getIsReadOnlyProperty() const
    {
        return true;
    }

    void NetworkSessionProperties::Add(const std::optional<int>& item)
    {
        properties_.push_back(item);
    }

    bool NetworkSessionProperties::Remove(const std::optional<int>& item)
    {
        auto it = std::find(properties_.begin(), properties_.end(), item);
        if (it == properties_.end())
            return false;
        properties_.erase(it);
        return true;
    }

    bool NetworkSessionProperties::Contains(const std::optional<int>& item) const
    {
        return std::find(properties_.begin(), properties_.end(), item) != properties_.end();
    }

    void NetworkSessionProperties::Clear()
    {
        properties_.clear();
    }

    System::Collections::Generic::IEnumerator<std::optional<int>>* NetworkSessionProperties::GetEnumerator()
    {
        return new Enumerator(properties_);
    }
}
