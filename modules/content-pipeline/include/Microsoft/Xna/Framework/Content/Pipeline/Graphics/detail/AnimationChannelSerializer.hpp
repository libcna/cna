// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics::detail
{
    /**
     * @brief Serializes an AnimationChannel the way XNA writes one: its keyframes as direct
     *        `<Keyframe>` children, with no wrapper element of their own.
     *
     * A channel is a collection, not a described type, and its keyframes carry
     * `[ContentSerializerIgnore]` on both of their properties -- yet each is written as
     * `<Keyframe><Time>…</Time><Transform>…</Transform></Keyframe>` (measured,
     * tests/reference/xna40/graphics case animation/serialize_content). Reading puts every
     * keyframe back through `Add`, so a document that lists them out of order still yields a
     * channel ordered by time (animation/deserialize_content).
     */
    class CNAEXT AnimationChannelSerializer final
        : public Serialization::Intermediate::ContentTypeSerializer<AnimationChannel>
    {
        using Base = Serialization::Intermediate::ContentTypeSerializer<AnimationChannel>;
        using ChildCallback = Serialization::Intermediate::ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        AnimationChannelSerializer() = default;

        /** @brief A channel is filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

    protected:
        /**
         * @brief Writes every keyframe of the channel.
         *
         * @param output The intermediate writer.
         * @param value The channel.
         * @param format The content format of the channel's own element.
         */
        void Serialize(Serialization::Intermediate::IntermediateWriter& output,
                       const std::shared_ptr<AnimationChannel>& value,
                       const Content::ContentSerializerAttribute& format) override
        {
            (void)format;
            Serialization::Intermediate::ContentTypeSerializerBase& keyframe =
                Serialization::Intermediate::IntermediateSerializer::TypeSerializerFor<AnimationKeyframe>();
            Content::ContentSerializerAttribute itemFormat;
            itemFormat.setElementNameProperty(std::string(AnimationChannel::CollectionItemName));
            for (const std::shared_ptr<AnimationKeyframe>& item : *value)
            {
                output.WriteObjectCore(Box<std::shared_ptr<AnimationKeyframe>>(item), itemFormat, keyframe, false);
            }
        }

        /**
         * @brief Reads every keyframe of the channel.
         *
         * @param input The intermediate reader.
         * @param format The content format of the channel's own element.
         * @param existingInstance The channel receiving the keyframes, or null.
         * @return The filled channel.
         */
        [[nodiscard]] std::shared_ptr<AnimationChannel> Deserialize(
            Serialization::Intermediate::IntermediateReader& input, const Content::ContentSerializerAttribute& format,
            std::shared_ptr<AnimationChannel> existingInstance) override
        {
            (void)format;
            if (existingInstance == nullptr)
            {
                existingInstance = std::make_shared<AnimationChannel>();
            }
            Serialization::Intermediate::ContentTypeSerializerBase& keyframe =
                Serialization::Intermediate::IntermediateSerializer::TypeSerializerFor<AnimationKeyframe>();
            Content::ContentSerializerAttribute itemFormat;
            itemFormat.setElementNameProperty(std::string(AnimationChannel::CollectionItemName));
            while (input.MoveToElement(itemFormat.getElementNameProperty()))
            {
                ContentObject item = input.ReadObjectCore(itemFormat, keyframe, ContentObject{});
                existingInstance->Add(Unbox<std::shared_ptr<AnimationKeyframe>>(item));
            }
            return existingInstance;
        }

        /**
         * @brief Announces every keyframe, so the namespace scan sees them.
         *
         * @param serializer The content serializer.
         * @param callback Invoked for each keyframe.
         * @param value The channel.
         */
        void ScanChildren(Serialization::Intermediate::IntermediateSerializer& serializer,
                          const ChildCallback& callback, const std::shared_ptr<AnimationChannel>& value) override
        {
            (void)serializer;
            Serialization::Intermediate::ContentTypeSerializerBase& keyframe =
                Serialization::Intermediate::IntermediateSerializer::TypeSerializerFor<AnimationKeyframe>();
            for (const std::shared_ptr<AnimationKeyframe>& item : *value)
            {
                callback(keyframe, Box<std::shared_ptr<AnimationKeyframe>>(item));
            }
        }
    };
}
