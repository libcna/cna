// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
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

    System::Collections::detail::ElementReference<std::optional<int>>
    NetworkSessionProperties::operator[](int index)
    {
        // Matches FNA exactly: assigning past the end appends instead of extending out to
        // `index` (the reference source itself has a "TODO: Expand list to index size?" comment).
        // The proxy can only bind an already-existing slot, so the append happens here, at
        // indexing time, whether the caller goes on to read or to write.
        if (index >= static_cast<int>(properties_.size()))
        {
            properties_.push_back(std::nullopt);
            ++version_;
            return {&properties_.back(), &version_};
        }
        return {&properties_[static_cast<std::size_t>(index)], &version_};
    }

    const std::optional<int>& NetworkSessionProperties::getItem(int index) const
    {
        return properties_.at(static_cast<std::size_t>(index));
    }

    void NetworkSessionProperties::setItem(int index, const std::optional<int>& value)
    {
        // XNA's indexer `set` accessor: appends when the index is past the end (FNA's
        // "TODO: Expand list to index size?" behavior), replaces in place otherwise.
        if (index >= static_cast<int>(properties_.size()))
        {
            properties_.push_back(value);
            ++version_;
            return;
        }
        System::Collections::detail::ElementReference<std::optional<int>>{
            &properties_[static_cast<std::size_t>(index)], &version_} = value;
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
        ++version_;
    }

    void NetworkSessionProperties::RemoveAt(int index)
    {
        properties_.erase(properties_.begin() + index);
        ++version_;
    }

    bool NetworkSessionProperties::getIsReadOnlyProperty() const
    {
        return true;
    }

    void NetworkSessionProperties::Add(const std::optional<int>& item)
    {
        properties_.push_back(item);
        ++version_;
    }

    bool NetworkSessionProperties::Remove(const std::optional<int>& item)
    {
        auto it = std::find(properties_.begin(), properties_.end(), item);
        if (it == properties_.end())
            return false;
        properties_.erase(it);
        ++version_;
        return true;
    }

    bool NetworkSessionProperties::Contains(const std::optional<int>& item) const
    {
        return std::find(properties_.begin(), properties_.end(), item) != properties_.end();
    }

    void NetworkSessionProperties::Clear()
    {
        properties_.clear();
        ++version_;
    }

    void NetworkSessionProperties::CopyTo(std::vector<std::optional<int>>& destination, int index) const
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
        if (index + getCountProperty() > static_cast<int>(destination.size()))
        {
            throw System::ArgumentException(
                "Destination array is not long enough to copy all the items in the collection.");
        }
        for (int i = 0; i < getCountProperty(); ++i)
        {
            destination[static_cast<std::size_t>(index + i)] = properties_[static_cast<std::size_t>(i)];
        }
    }

    System::Collections::Generic::IEnumerator<std::optional<int>>* NetworkSessionProperties::GetEnumerator()
    {
        return new Enumerator(properties_);
    }
}
