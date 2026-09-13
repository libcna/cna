// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"

#include <algorithm>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        /** @brief The refusal .NET's List<T> gives for an index outside the collection. */
        [[noreturn]] void ThrowIndexOutOfRange()
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
    }

    // ------------------------------------------------------------------------------------------
    // AnimationKeyframe
    // ------------------------------------------------------------------------------------------

    AnimationKeyframe::AnimationKeyframe(System::TimeSpan time, Matrix transform)
        : time_(time), transform_(transform)
    {
    }

    System::TimeSpan AnimationKeyframe::getTimeProperty() const noexcept { return time_; }

    const Matrix& AnimationKeyframe::getTransformProperty() const noexcept { return transform_; }

    void AnimationKeyframe::setTransformProperty(Matrix value) noexcept { transform_ = value; }

    SharpRuntime::intcs AnimationKeyframe::CompareTo(const AnimationKeyframe& other) const noexcept
    {
        if (time_ < other.time_)
        {
            return -1;
        }
        return time_ > other.time_ ? 1 : 0;
    }

    void AnimationKeyframe::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<AnimationKeyframe>& d)
    {
        d.Field("Time", &AnimationKeyframe::time_);
        d.Field("Transform", &AnimationKeyframe::transform_);
    }

    const std::string& AnimationKeyframe::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string AnimationKeyframe::ToString() const { return GetTypeName(); }

    // ------------------------------------------------------------------------------------------
    // AnimationChannel
    // ------------------------------------------------------------------------------------------

    SharpRuntime::intcs AnimationChannel::getCountProperty() const noexcept
    {
        return static_cast<SharpRuntime::intcs>(keyframes_.size());
    }

    const std::shared_ptr<AnimationKeyframe>& AnimationChannel::operator[](SharpRuntime::intcs index) const
    {
        if (index < 0 || static_cast<std::size_t>(index) >= keyframes_.size())
        {
            ThrowIndexOutOfRange();
        }
        return keyframes_[static_cast<std::size_t>(index)];
    }

    SharpRuntime::intcs AnimationChannel::Add(const std::shared_ptr<AnimationKeyframe>& item)
    {
        if (item == nullptr)
        {
            throw System::ArgumentNullException("item");
        }
        // The channel stays ordered by time, and a keyframe added at a time another already
        // occupies goes after it (measured, animation/channel_duplicate_time).
        const auto position = std::upper_bound(keyframes_.begin(), keyframes_.end(), item,
                                               [](const std::shared_ptr<AnimationKeyframe>& left,
                                                  const std::shared_ptr<AnimationKeyframe>& right)
                                               { return left->getTimeProperty() < right->getTimeProperty(); });
        const auto index = static_cast<SharpRuntime::intcs>(std::distance(keyframes_.begin(), position));
        keyframes_.insert(position, item);
        return index;
    }

    void AnimationChannel::Clear() noexcept { keyframes_.clear(); }

    bool AnimationChannel::Contains(const std::shared_ptr<AnimationKeyframe>& item) const noexcept
    {
        return IndexOf(item) >= 0;
    }

    SharpRuntime::intcs AnimationChannel::IndexOf(const std::shared_ptr<AnimationKeyframe>& item) const noexcept
    {
        // Membership is by reference, as XNA's is (animation/channel_contains_and_indexof).
        const auto found = std::find(keyframes_.begin(), keyframes_.end(), item);
        return found == keyframes_.end() ? -1
                                         : static_cast<SharpRuntime::intcs>(std::distance(keyframes_.begin(), found));
    }

    bool AnimationChannel::Remove(const std::shared_ptr<AnimationKeyframe>& item)
    {
        const auto found = std::find(keyframes_.begin(), keyframes_.end(), item);
        if (found == keyframes_.end())
        {
            return false;
        }
        keyframes_.erase(found);
        return true;
    }

    void AnimationChannel::RemoveAt(SharpRuntime::intcs index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= keyframes_.size())
        {
            ThrowIndexOutOfRange();
        }
        keyframes_.erase(keyframes_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    std::vector<std::shared_ptr<AnimationKeyframe>>::const_iterator AnimationChannel::begin() const noexcept
    {
        return keyframes_.begin();
    }

    std::vector<std::shared_ptr<AnimationKeyframe>>::const_iterator AnimationChannel::end() const noexcept
    {
        return keyframes_.end();
    }

    const std::string& AnimationChannel::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string AnimationChannel::ToString() const { return GetTypeName(); }

    // ------------------------------------------------------------------------------------------
    // The dictionaries and the animation itself
    // ------------------------------------------------------------------------------------------

    std::string AnimationChannelDictionary::ToString() const { return std::string(XnaTypeName); }

    AnimationChannelDictionary& AnimationContent::getChannelsProperty() noexcept { return channels_; }

    const AnimationChannelDictionary& AnimationContent::getChannelsProperty() const noexcept { return channels_; }

    System::TimeSpan AnimationContent::getDurationProperty() const noexcept { return duration_; }

    void AnimationContent::setDurationProperty(System::TimeSpan value) noexcept { duration_ = value; }

    void AnimationContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<AnimationContent>& d)
    {
        // Neither member is optional: an animation with no channels still writes <Channels />
        // (measured, animation/serialize_empty_content), and a document without <Duration> is
        // refused (animation/deserialize_no_duration).
        d.BaseType<ContentItem>();
        d.Property("Duration", &AnimationContent::getDurationProperty, &AnimationContent::setDurationProperty);
        d.ReadOnlyProperty("Channels", [](AnimationContent& animation) -> AnimationChannelDictionary&
                           { return animation.getChannelsProperty(); });
    }

    const std::string& AnimationContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string AnimationContent::ToString() const { return GetTypeName(); }

    std::string AnimationContentDictionary::ToString() const { return std::string(XnaTypeName); }
}
