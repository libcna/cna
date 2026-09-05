// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/NamedValueDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/detail/IntermediateSerializerCore.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Serializes a `NamedValueDictionary<T>` as XNA does: one `<Data Key="…">` element per
     *        entry, with a `Type` attribute only when the value's type is not the dictionary's
     *        default serializer type (`tests/reference/xna40/intermediate/opaque_data_dictionary.xml`).
     *
     * @tparam TDictionary The dictionary type (a `NamedValueDictionary<TValue>`).
     * @tparam TValue The dictionary's value type.
     */
    template<typename TDictionary, typename TValue>
    class CNAEXT NamedValueDictionarySerializer final : public ContentTypeSerializer<TDictionary>
    {
        using Value = detail::DeclaredContentType<TValue>;
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        NamedValueDictionarySerializer() = default;

        /** @brief Dictionaries are filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

        /** @brief An empty dictionary has nothing to write. */
        [[nodiscard]] bool ObjectIsEmpty(const TDictionary& value) const override { return value.getCountProperty() == 0; }

    protected:
        void Serialize(IntermediateWriter& output, const TDictionary& value, const ContentSerializerAttribute& format) override
        {
            (void)format;
            ContentTypeSerializerBase& defaultSerializer = Default(value);
            ContentTypeSerializerBase& object = output.getSerializerProperty().GetTypeSerializer(System::Type::From<System::Object>());
            ContentSerializerAttribute dataFormat;
            dataFormat.setElementNameProperty("Data");
            System::Xml::XmlWriter& xml = output.getXmlProperty();
            for (const auto& [key, item] : value)
            {
                ContentObject payload = ToContentObject<Value>(item);
                ContentTypeSerializerBase& dynamic = output.ResolveDynamic(payload, object);
                xml.WriteStartElement("Data");
                xml.WriteAttributeString("Key", key);
                if (&dynamic != &defaultSerializer)
                {
                    xml.WriteAttributeString("Type", output.getSerializerProperty().SpellTypeName(dynamic));
                }
                dynamic.InvokeSerialize(output, payload, dataFormat);
                xml.WriteEndElement();
            }
        }

        [[nodiscard]] TDictionary Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                              TDictionary existingInstance) override
        {
            (void)format;
            ContentTypeSerializerBase& defaultSerializer = Default(existingInstance);
            ContentSerializerAttribute dataFormat;
            dataFormat.setElementNameProperty("Data");
            while (input.MoveToElement("Data"))
            {
                const std::string key = input.getXmlProperty().GetAttribute("Key");
                if (key.empty())
                {
                    throw InvalidContentException("XML attribute \"Key\" was not found.");
                }
                const std::string spelledType = input.getXmlProperty().GetAttribute("Type");
                ContentTypeSerializerBase& serializer =
                    spelledType.empty() ? defaultSerializer
                                        : input.getSerializerProperty().ResolveTypeName(spelledType, input.getXmlProperty());
                ContentObject item = input.ReadRawObjectCore(dataFormat, serializer, ContentObject{});
                existingInstance.Add(key, FromContentObject<Value>(item));
            }
            return existingInstance;
        }

        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const TDictionary& value) override
        {
            ContentTypeSerializerBase& object = serializer.GetTypeSerializer(System::Type::From<System::Object>());
            for (const auto& [key, item] : value)
            {
                callback(object, ToContentObject<Value>(item));
            }
        }

    private:
        static ContentTypeSerializerBase& Default(const TDictionary& value)
        {
            // Friendship is granted by NamedValueDictionary<TValue>, so the protected virtual is
            // reached through that base; dispatch still lands on the dictionary's own override.
            const NamedValueDictionary<TValue>& base = value;
            return IntermediateSerializer::RequireTypeSerializer(base.getDefaultSerializerTypeProperty(),
                                                                 "the default serializer type of " +
                                                                     ContentTypeName<TDictionary>::Name());
        }
    };
}
