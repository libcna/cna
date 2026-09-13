// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannel.hpp"

#include <algorithm>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/VertexChannelSerializers.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    VertexChannelBase::VertexChannelBase(std::string name) : name_(std::move(name)) {}

    const std::string& VertexChannelBase::getNameProperty() const noexcept { return name_; }

    void VertexChannelBase::CopyTo(std::vector<ContentObject>& destination, SharpRuntime::intcs index) const
    {
        if (index < 0)
        {
            throw System::ArgumentOutOfRangeException("index");
        }
        if (destination.size() < static_cast<std::size_t>(index + getCountProperty()))
        {
            throw System::ArgumentException("The channel does not fit into the destination.", "destination");
        }
        for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
        {
            destination[static_cast<std::size_t>(index + i)] = (*this)[i];
        }
    }

    void VertexChannelBase::ThrowIndexOutOfRange()
    {
        throw System::ArgumentOutOfRangeException(
            "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
    }

    void VertexChannelBase::ThrowNoVectorConversion(System::Type elementType)
    {
        throw System::InvalidOperationException("Vertex channel element type " + elementType.getFullNameProperty() +
                                                " has no vector conversion.");
    }

    const std::string& VertexChannelBase::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string VertexChannelBase::ToString() const { return std::string(XnaTypeName); }

    // ------------------------------------------------------------------------------------------
    // VertexChannelCollection
    // ------------------------------------------------------------------------------------------

    VertexChannelCollection::VertexChannelCollection(VertexContent* owner) noexcept : owner_(owner) {}

    SharpRuntime::intcs VertexChannelCollection::getCountProperty() const noexcept
    {
        return static_cast<SharpRuntime::intcs>(channels_.size());
    }

    const std::shared_ptr<VertexChannelBase>& VertexChannelCollection::operator[](SharpRuntime::intcs index) const
    {
        if (index < 0 || static_cast<std::size_t>(index) >= channels_.size())
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
        return channels_[static_cast<std::size_t>(index)];
    }

    const std::shared_ptr<VertexChannelBase>& VertexChannelCollection::operator[](const std::string& name) const
    {
        return channels_[static_cast<std::size_t>(RequireIndexOf(name))];
    }

    std::shared_ptr<VertexChannelBase> VertexChannelCollection::Add(const std::string& name,
                                                                    System::Type elementType,
                                                                    const std::vector<ContentObject>& channelData)
    {
        return Insert(getCountProperty(), name, elementType, channelData);
    }

    std::shared_ptr<VertexChannelBase> VertexChannelCollection::Insert(SharpRuntime::intcs index,
                                                                      const std::string& name,
                                                                      System::Type elementType,
                                                                      const std::vector<ContentObject>& channelData)
    {
        RequireFreeName(name);
        RequireChannelSize(name, static_cast<SharpRuntime::intcs>(channelData.size()));
        // The element type is named at run time, so the channel comes from the factory the element
        // types register with -- XNA reflects a VertexChannel<T> into being at this point.
        const std::shared_ptr<VertexChannelBase> channel = detail::VertexChannelFactory::Create(elementType, name);
        for (const ContentObject& value : channelData)
        {
            channel->AddEntry(value);
        }
        InsertChannel(index, channel);
        return channel;
    }

    void VertexChannelCollection::Clear() noexcept { channels_.clear(); }

    bool VertexChannelCollection::Contains(const std::shared_ptr<VertexChannelBase>& channel) const noexcept
    {
        return IndexOf(channel) >= 0;
    }

    bool VertexChannelCollection::Contains(const std::string& name) const noexcept { return IndexOf(name) >= 0; }

    SharpRuntime::intcs VertexChannelCollection::IndexOf(
        const std::shared_ptr<VertexChannelBase>& channel) const noexcept
    {
        const auto found = std::find(channels_.begin(), channels_.end(), channel);
        return found == channels_.end() ? -1
                                        : static_cast<SharpRuntime::intcs>(std::distance(channels_.begin(), found));
    }

    SharpRuntime::intcs VertexChannelCollection::IndexOf(const std::string& name) const noexcept
    {
        for (std::size_t i = 0; i < channels_.size(); ++i)
        {
            if (channels_[i] != nullptr && channels_[i]->getNameProperty() == name)
            {
                return static_cast<SharpRuntime::intcs>(i);
            }
        }
        return -1;
    }

    bool VertexChannelCollection::Remove(const std::shared_ptr<VertexChannelBase>& channel)
    {
        const SharpRuntime::intcs index = IndexOf(channel);
        if (index < 0)
        {
            return false;
        }
        RemoveAt(index);
        return true;
    }

    bool VertexChannelCollection::Remove(const std::string& name)
    {
        const SharpRuntime::intcs index = IndexOf(name);
        if (index < 0)
        {
            return false;
        }
        RemoveAt(index);
        return true;
    }

    void VertexChannelCollection::RemoveAt(SharpRuntime::intcs index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= channels_.size())
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
        channels_.erase(channels_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    std::vector<std::shared_ptr<VertexChannelBase>>::const_iterator VertexChannelCollection::begin() const noexcept
    {
        return channels_.begin();
    }

    std::vector<std::shared_ptr<VertexChannelBase>>::const_iterator VertexChannelCollection::end() const noexcept
    {
        return channels_.end();
    }

    void VertexChannelCollection::InsertChannel(SharpRuntime::intcs index,
                                                const std::shared_ptr<VertexChannelBase>& channel)
    {
        if (channel == nullptr)
        {
            throw System::ArgumentNullException("channel");
        }
        if (index < 0 || static_cast<std::size_t>(index) > channels_.size())
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
        RequireFreeName(channel->getNameProperty());
        channels_.insert(channels_.begin() + static_cast<std::ptrdiff_t>(index), channel);
    }

    SharpRuntime::intcs VertexChannelCollection::RequireIndexOf(const std::string& name) const
    {
        const SharpRuntime::intcs index = IndexOf(name);
        if (index < 0)
        {
            throw System::Collections::Generic::KeyNotFoundException("Vertex channel \"" + name +
                                                                     "\" was not found.");
        }
        return index;
    }

    void VertexChannelCollection::RequireFreeName(const std::string& name) const
    {
        if (IndexOf(name) >= 0)
        {
            throw System::ArgumentException("VertexChannelCollection already contains a channel with name \"" + name +
                                            "\".");
        }
    }

    void VertexChannelCollection::RequireChannelSize(const std::string& name, SharpRuntime::intcs size) const
    {
        if (owner_ == nullptr || size == 0)
        {
            return;
        }
        const SharpRuntime::intcs expected = owner_->getVertexCountProperty();
        if (size != expected)
        {
            throw System::ArgumentException("Wrong number of VertexChannel entries in \"" + name +
                                            "\". Channel size is " + std::to_string(size) +
                                            ", but the parent VertexContent has a count of " +
                                            std::to_string(expected) + ".");
        }
    }

    void VertexChannelCollection::Replace(SharpRuntime::intcs index, const std::shared_ptr<VertexChannelBase>& channel)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= channels_.size())
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
        channels_[static_cast<std::size_t>(index)] = channel;
    }

    void VertexChannelCollection::ThrowWrongType(const std::string& name, const std::string& actual,
                                                 const std::string& expected)
    {
        throw System::InvalidOperationException("Vertex channel \"" + name +
                                                "\" is the wrong type. It has element type " + actual + ". Type " +
                                                expected + " is expected.");
    }

    const std::string& VertexChannelCollection::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string VertexChannelCollection::ToString() const { return std::string(XnaTypeName); }
}
