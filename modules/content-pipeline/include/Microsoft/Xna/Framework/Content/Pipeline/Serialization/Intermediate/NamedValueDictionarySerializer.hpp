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
     * @brief Serializes a `NamedValueDictionary<T>` as XNA does: one element per entry carrying a
     *        `Key` attribute, with a `Type` attribute only when the value's type is not the
     *        dictionary's default serializer type
     *        (`tests/reference/xna40/intermediate/opaque_data_dictionary.xml`).
     *
     * The item element is `Data` unless the dictionary declares another through
     * `[ContentSerializerCollectionItemName]`, as `TextureReferenceDictionary` declares `Texture`
     * (measured, tests/reference/xna40/graphics case material/serialize_basic).
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
        /**
         * @brief Creates the serializer.
         *
         * @param itemElementName The element each entry is written as; `Data` unless the
         *        dictionary declares a collection item name.
         */
        explicit NamedValueDictionarySerializer(std::string itemElementName = "Data")
            : itemElementName_(std::move(itemElementName))
        {
        }

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
            dataFormat.setElementNameProperty(itemElementName_);
            System::Xml::XmlWriter& xml = output.getXmlProperty();
            for (const auto& [key, item] : value)
            {
                ContentObject payload = ToContentObject<Value>(item);
                ContentTypeSerializerBase& dynamic = output.ResolveDynamic(payload, object);
                xml.WriteStartElement(itemElementName_);
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
            dataFormat.setElementNameProperty(itemElementName_);
            while (input.MoveToElement(itemElementName_))
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
            // An entry needs a Type attribute -- and therefore its namespace's alias -- exactly
            // when its type is not the dictionary's default one, which is the rule Serialize
            // above applies. Announcing every entry as an object instead made a texture
            // dictionary declare an alias XNA does not (tests/reference/xna40/graphics case
            // material/serialize_basic); announcing every entry as the default one makes the
            // scan try to read a float entry as the default string of an opaque data dictionary.
            ContentTypeSerializerBase& object = serializer.GetTypeSerializer(System::Type::From<System::Object>());
            ContentTypeSerializerBase& defaultSerializer = Default(value);
            for (const auto& [key, item] : value)
            {
                ContentObject payload = ToContentObject<Value>(item);
                ContentTypeSerializerBase* dynamic = IntermediateSerializer::FindTypeSerializer(payload);
                callback(dynamic == &defaultSerializer ? defaultSerializer : object, payload);
            }
        }

    private:
        std::string itemElementName_;

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
