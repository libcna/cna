// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/NamedValueDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides methods and properties for maintaining an animation keyframe: one
     *        transform at one time.
     *
     * Keyframes compare by time and nothing else, and they are compared by reference for
     * membership: two keyframes with the same time and transform are not equal (measured,
     * tests/reference/xna40/graphics case animation/keyframe_compare).
     */
    class AnimationKeyframe : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe";

        /**
         * @brief Initializes an empty keyframe, for the intermediate serializer to fill.
         *
         * XNA reaches a private constructor by reflection; C++ has no reflection, so the
         * serializer needs one it can call.
         */
        CNAEXT AnimationKeyframe() = default;

        /**
         * @brief Initializes a new instance of AnimationKeyframe.
         *
         * @param time The time offset of this keyframe from the start of the animation.
         * @param transform The transform of this keyframe.
         */
        AnimationKeyframe(System::TimeSpan time, Matrix transform);

        /**
         * @brief Gets the time offset of this keyframe from the start of the animation.
         *
         * @return The time offset.
         */
        [[nodiscard]] System::TimeSpan getTimeProperty() const noexcept;

        /**
         * @brief Gets the transform of this keyframe.
         *
         * @return The transform.
         */
        [[nodiscard]] const Matrix& getTransformProperty() const noexcept;

        /**
         * @brief Sets the transform of this keyframe.
         *
         * @param value The transform.
         */
        void setTransformProperty(Matrix value) noexcept;

        /**
         * @brief Compares this keyframe with another by time.
         *
         * @param other The keyframe to compare with.
         * @return -1, 0 or 1 as this keyframe's time is earlier than, equal to or later than the
         *         other's.
         */
        [[nodiscard]] SharpRuntime::intcs CompareTo(const AnimationKeyframe& other) const noexcept;

        /**
         * @brief Describes the keyframe for the intermediate serializer.
         *
         * Both properties carry `[ContentSerializerIgnore]` in XNA, and both are nevertheless
         * written -- as `<Time>` then `<Transform>` inside each `<Keyframe>` element (measured,
         * animation/serialize_content) -- because the channel writes them itself.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<AnimationKeyframe>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        System::TimeSpan time_{};
        Matrix transform_{};
    };

    /**
     * @brief Provides methods and properties for maintaining a list of animation keyframes,
     *        always ordered by time.
     *
     * Adding a keyframe inserts it at its place in time and answers that index; a keyframe added
     * at a time another already occupies goes after it (measured, animation/channel_sorted and
     * animation/channel_duplicate_time).
     */
    class AnimationChannel : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationChannel";

        /** @brief The element each keyframe is written as. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Keyframe";

        /** @brief Initializes a new instance of AnimationChannel. */
        AnimationChannel() = default;

        /**
         * @brief Gets the number of keyframes in the channel.
         *
         * @return The keyframe count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const noexcept;

        /**
         * @brief Gets the keyframe at the given index.
         *
         * @param index The index of the keyframe.
         * @return The keyframe.
         * @throws System::ArgumentOutOfRangeException when the index is outside the channel.
         */
        [[nodiscard]] const std::shared_ptr<AnimationKeyframe>& operator[](SharpRuntime::intcs index) const;

        /**
         * @brief Adds a keyframe at its place in time.
         *
         * @param item The keyframe to add.
         * @return The index the keyframe was inserted at.
         * @throws System::ArgumentNullException when the keyframe is null.
         */
        SharpRuntime::intcs Add(const std::shared_ptr<AnimationKeyframe>& item);

        /** @brief Removes every keyframe from the channel. */
        void Clear() noexcept;

        /**
         * @brief Determines whether the channel contains the given keyframe.
         *
         * Membership is by reference, as it is in XNA: a keyframe equal in time and transform to
         * one in the channel is not contained.
         *
         * @param item The keyframe to look for.
         * @return true when this exact keyframe is in the channel.
         */
        [[nodiscard]] bool Contains(const std::shared_ptr<AnimationKeyframe>& item) const noexcept;

        /**
         * @brief Gets the index of the given keyframe.
         *
         * @param item The keyframe to look for.
         * @return The index, or -1 when this exact keyframe is not in the channel.
         */
        [[nodiscard]] SharpRuntime::intcs IndexOf(const std::shared_ptr<AnimationKeyframe>& item) const noexcept;

        /**
         * @brief Removes the given keyframe from the channel.
         *
         * @param item The keyframe to remove.
         * @return true when it was there.
         */
        bool Remove(const std::shared_ptr<AnimationKeyframe>& item);

        /**
         * @brief Removes the keyframe at the given index.
         *
         * @param index The index of the keyframe to remove.
         * @throws System::ArgumentOutOfRangeException when the index is outside the channel.
         */
        void RemoveAt(SharpRuntime::intcs index);

        /**
         * @brief Returns an iterator to the first keyframe, so the channel can be traversed with a
         *        range-based for loop -- the C++ form of XNA's `GetEnumerator`.
         *
         * @return An iterator to the first keyframe.
         */
        [[nodiscard]] std::vector<std::shared_ptr<AnimationKeyframe>>::const_iterator begin() const noexcept;

        /**
         * @brief Returns an iterator past the last keyframe.
         *
         * @return An iterator past the last keyframe.
         */
        [[nodiscard]] std::vector<std::shared_ptr<AnimationKeyframe>>::const_iterator end() const noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        std::vector<std::shared_ptr<AnimationKeyframe>> keyframes_;
    };

    /**
     * @brief Collection of animation channels, one per bone the animation moves.
     */
    class AnimationChannelDictionary final : public NamedValueDictionary<std::shared_ptr<AnimationChannel>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationChannelDictionary";

        /** @brief The element each entry is written as. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Channel";

        /** @brief Initializes a new instance of AnimationChannelDictionary. */
        AnimationChannelDictionary() = default;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        CNAEXT [[nodiscard]] std::string ToString() const;

    protected:
        /**
         * @brief Gets the serializer type of an entry, which is why no entry carries a `Type`
         *        attribute.
         *
         * @return `System::Type::From<AnimationChannel>()`.
         */
        [[nodiscard]] System::Type getDefaultSerializerTypeProperty() const override
        {
            return System::Type::From<AnimationChannel>();
        }
    };

    /**
     * @brief Provides properties for maintaining an animation: its duration and one channel per
     *        animated bone.
     */
    class AnimationContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationContent";

        /** @brief Initializes a new instance of AnimationContent. */
        AnimationContent() = default;

        /**
         * @brief Gets the collection of animation channels.
         *
         * @return The channels, keyed by the name of the bone each animates.
         */
        [[nodiscard]] AnimationChannelDictionary& getChannelsProperty() noexcept;

        /**
         * @brief Gets the collection of animation channels.
         *
         * @return The channels, keyed by the name of the bone each animates.
         */
        [[nodiscard]] const AnimationChannelDictionary& getChannelsProperty() const noexcept;

        /**
         * @brief Gets the total length of the animation.
         *
         * @return The duration.
         */
        [[nodiscard]] System::TimeSpan getDurationProperty() const noexcept;

        /**
         * @brief Sets the total length of the animation.
         *
         * @param value The duration.
         */
        void setDurationProperty(System::TimeSpan value) noexcept;

        /**
         * @brief Describes the animation for the intermediate serializer: ContentItem's members,
         *        then the duration and the channels, neither of which may be left out.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<AnimationContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        AnimationChannelDictionary channels_;
        System::TimeSpan duration_{};
    };

    /**
     * @brief Collection of animations, keyed by name.
     */
    class AnimationContentDictionary final : public NamedValueDictionary<std::shared_ptr<AnimationContent>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationContentDictionary";

        /** @brief The element each entry is written as. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Animation";

        /** @brief Initializes a new instance of AnimationContentDictionary. */
        AnimationContentDictionary() = default;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        CNAEXT [[nodiscard]] std::string ToString() const;

    protected:
        /**
         * @brief Gets the serializer type of an entry, which is why no entry carries a `Type`
         *        attribute.
         *
         * @return `System::Type::From<AnimationContent>()`.
         */
        [[nodiscard]] System::Type getDefaultSerializerTypeProperty() const override
        {
            return System::Type::From<AnimationContent>();
        }
    };
}
