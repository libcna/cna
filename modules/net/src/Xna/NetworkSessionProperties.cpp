// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Net
{
    // The fixed eight-slot reference contract supersedes the variable-list
    // approximation.
    NetworkSessionProperties::NetworkSessionProperties() = default;
    NetworkSessionProperties::NetworkSessionProperties(const NetworkSessionProperties& other)
        : properties_(other.properties_)
    {
        if (other.readOnly_)
            makeReadOnly();
    }
    NetworkSessionProperties::NetworkSessionProperties(NetworkSessionProperties&& other)
        : NetworkSessionProperties(static_cast<const NetworkSessionProperties&>(other))
    {
    }
    NetworkSessionProperties& NetworkSessionProperties::operator=(const NetworkSessionProperties& other)
    {
        if (beforeWrite_)
            beforeWrite_();
        properties_ = other.properties_;
        ++version_;
        return *this;
    }
    NetworkSessionProperties& NetworkSessionProperties::operator=(NetworkSessionProperties&& other)
    {
        return operator=(static_cast<const NetworkSessionProperties&>(other));
    }
    int NetworkSessionProperties::getCountProperty() const
    {
        return 8;
    }
    const std::optional<int>& NetworkSessionProperties::getItem(int index) const
    {
        if (index < 0 || index >= 8)
            throw System::ArgumentOutOfRangeException("index");
        return properties_[static_cast<std::size_t>(index)];
    }
    const std::optional<int>& NetworkSessionProperties::operator[](int index) const
    {
        return getItem(index);
    }
    System::Collections::detail::ElementReference<std::optional<int>> NetworkSessionProperties::operator[](
        int index)
    {
        (void)getItem(index);
        return {&properties_[static_cast<std::size_t>(index)], &version_, &beforeWrite_};
    }
    void NetworkSessionProperties::setItem(int index, const std::optional<int>& value)
    {
        operator[](index) = value;
    }
    int NetworkSessionProperties::IndexOf(const std::optional<int>& item) const
    {
        const auto found = std::find(properties_.begin(), properties_.end(), item);
        return found == properties_.end() ? -1 : static_cast<int>(std::distance(properties_.begin(), found));
    }
    void NetworkSessionProperties::Insert(int, const std::optional<int>&)
    {
        throw System::NotSupportedException("Session properties have a fixed size.");
    }
    void NetworkSessionProperties::RemoveAt(int)
    {
        throw System::NotSupportedException("Session properties have a fixed size.");
    }
    bool NetworkSessionProperties::getIsReadOnlyProperty() const
    {
        // The reference collection flag stays false even for a write-rejecting snapshot.
        return false;
    }
    void NetworkSessionProperties::Add(const std::optional<int>&)
    {
        throw System::NotSupportedException("Session properties have a fixed size.");
    }
    bool NetworkSessionProperties::Remove(const std::optional<int>&)
    {
        throw System::NotSupportedException("Session properties have a fixed size.");
    }
    bool NetworkSessionProperties::Contains(const std::optional<int>& item) const
    {
        return IndexOf(item) >= 0;
    }
    void NetworkSessionProperties::Clear()
    {
        throw System::NotSupportedException("Session properties have a fixed size.");
    }
    void NetworkSessionProperties::CopyTo(std::vector<std::optional<int>>& destination, int index) const
    {
        System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
        if (static_cast<std::size_t>(index) > destination.size() ||
            properties_.size() > destination.size() - static_cast<std::size_t>(index))
            throw System::ArgumentException("Destination array is too small.");
        std::copy(properties_.begin(), properties_.end(), destination.begin() + index);
    }
    System::Collections::Generic::IEnumerator<std::optional<int>>* NetworkSessionProperties::GetEnumerator()
    {
        return new Enumerator(properties_);
    }
    void NetworkSessionProperties::setWriteGuard(std::function<void()> guard)
    {
        readOnly_ = false;
        beforeWrite_ = std::move(guard);
    }
    void NetworkSessionProperties::makeReadOnly()
    {
        readOnly_ = true;
        beforeWrite_ = [] {
            throw System::NotSupportedException("Advertised session properties are read only.");
        };
    }
    void NetworkSessionProperties::replaceFromTransport(const NetworkSessionProperties& properties)
    {
        properties_ = properties.properties_;
        ++version_;
    }
} // namespace Microsoft::Xna::Framework::Net
