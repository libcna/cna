// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannel.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics::detail
{
    /**
     * @brief Builds a vertex channel of an element type named at run time.
     *
     * XNA reads the `ElementType` attribute and reflects a `VertexChannel<T>` into being; C++ has
     * no reflection, so each element type registers a factory here, exactly as the bitmap types
     * register theirs.
     */
    class CNAEXT VertexChannelFactory final
    {
    public:
        /** @brief Creates a channel with a name and no entries. */
        using Factory = std::function<std::shared_ptr<VertexChannelBase>(const std::string&)>;

        /**
         * @brief Registers the factory of one element type.
         *
         * @tparam T The element type.
         */
        template<typename T>
        static void Register()
        {
            Register(System::Type::From<T>(), [](const std::string& name)
                     { return std::static_pointer_cast<VertexChannelBase>(
                           std::make_shared<VertexChannel<T>>(name)); });
        }

        /**
         * @brief Registers the factory of one element type.
         *
         * @param elementType The element type.
         * @param factory Creates an empty channel of that type.
         */
        static void Register(System::Type elementType, Factory factory);

        /**
         * @brief Creates an empty channel of the given element type.
         *
         * @param elementType The element type.
         * @param name The channel name.
         * @return The new channel.
         * @throws InvalidContentException when no factory is registered for the type.
         */
        [[nodiscard]] static std::shared_ptr<VertexChannelBase> Create(System::Type elementType,
                                                                      const std::string& name);

        /** @brief Registers every element type the pipeline knows. Runs once. */
        static void RegisterBuiltIns();
    };

    /**
     * @brief Serializes a typed vertex channel as its packed entries, which is how the position
     *        indices of a vertex content are written (`<PositionIndices>0 1 2</PositionIndices>`,
     *        measured in tests/reference/xna40/graphics case mesh/serialize).
     *
     * @tparam T The element type.
     */
    template<typename T>
    class CNAEXT VertexChannelSerializer final
        : public Serialization::Intermediate::ContentTypeSerializer<VertexChannel<T>>
    {
        using ChildCallback = Serialization::Intermediate::ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        VertexChannelSerializer() = default;

        /** @brief A channel is filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

    protected:
        /** @brief Writes the entries as packed text. */
        void Serialize(Serialization::Intermediate::IntermediateWriter& output,
                       const std::shared_ptr<VertexChannel<T>>& value,
                       const Content::ContentSerializerAttribute& format) override
        {
            (void)format;
            const std::string text = value->PackedContent();
            if (!text.empty())
            {
                output.getXmlProperty().WriteString(text);
            }
        }

        /** @brief Reads the entries from packed text. */
        [[nodiscard]] std::shared_ptr<VertexChannel<T>> Deserialize(
            Serialization::Intermediate::IntermediateReader& input,
            const Content::ContentSerializerAttribute& format,
            std::shared_ptr<VertexChannel<T>> existingInstance) override
        {
            (void)format;
            if (existingInstance == nullptr)
            {
                existingInstance = std::make_shared<VertexChannel<T>>(std::string());
            }
            existingInstance->SetPackedContent(
                Serialization::Intermediate::IntermediateReader::SplitTokens(input.ReadContentText()));
            return existingInstance;
        }
    };

    /**
     * @brief Serializes a vertex channel collection as XNA writes one: a `<VertexChannel>` element
     *        per channel, carrying its name and element type as attributes and its entries as
     *        packed text (measured, mesh/serialize).
     */
    class CNAEXT VertexChannelCollectionSerializer final
        : public Serialization::Intermediate::ContentTypeSerializer<VertexChannelCollection>
    {
        using ChildCallback = Serialization::Intermediate::ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        VertexChannelCollectionSerializer() = default;

        /** @brief The collection is filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

        /** @brief An empty collection still writes its element, as the measurement shows. */
        [[nodiscard]] bool ObjectIsEmpty(const std::shared_ptr<VertexChannelCollection>& value) const override
        {
            (void)value;
            return false;
        }

    protected:
        /**
         * @brief Writes every channel.
         *
         * @param output The intermediate writer.
         * @param value The collection.
         * @param format The content format of the collection's own element.
         */
        void Serialize(Serialization::Intermediate::IntermediateWriter& output,
                       const std::shared_ptr<VertexChannelCollection>& value,
                       const Content::ContentSerializerAttribute& format) override;

        /**
         * @brief Reads every channel.
         *
         * @param input The intermediate reader.
         * @param format The content format of the collection's own element.
         * @param existingInstance The collection receiving the channels.
         * @return The filled collection.
         */
        [[nodiscard]] std::shared_ptr<VertexChannelCollection> Deserialize(
            Serialization::Intermediate::IntermediateReader& input,
            const Content::ContentSerializerAttribute& format,
            std::shared_ptr<VertexChannelCollection> existingInstance) override;

        /**
         * @brief Announces each channel's element type, so the namespace scan declares its alias.
         *
         * @param serializer The content serializer.
         * @param callback Invoked for each channel entry.
         * @param value The collection.
         */
        void ScanChildren(Serialization::Intermediate::IntermediateSerializer& serializer,
                          const ChildCallback& callback,
                          const std::shared_ptr<VertexChannelCollection>& value) override;
    };
}
