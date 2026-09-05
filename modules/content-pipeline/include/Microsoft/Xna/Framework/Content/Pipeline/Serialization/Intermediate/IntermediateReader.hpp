// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/detail/IntermediateSerializerCore.hpp"
#include "System/Type.hpp"
#include "System/Xml/XmlReader.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Provides an implementation of many of the methods of IntermediateSerializer including
     *        method related to reading intermediate XML.
     */
    class IntermediateReader final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader";

        /**
         * @brief Gets the parent serializer.
         *
         * @return The serializer performing this pass.
         */
        [[nodiscard]] IntermediateSerializer& getSerializerProperty() const noexcept;

        /**
         * @brief Gets the XML input stream.
         *
         * @return The reader.
         */
        [[nodiscard]] System::Xml::XmlReader& getXmlProperty() const noexcept;

        /**
         * @brief Moves to the specified element if the reader is positioned on its start tag.
         *
         * @param elementName The element name.
         * @return True when the current content node is a start element with that name.
         */
        bool MoveToElement(const std::string& elementName);

        /**
         * @brief Reads an external reference ID and records it for subsequent operations.
         *
         * @tparam T The type of the referenced content.
         * @param existingInstance The external reference whose filename is set once the
         *        `ExternalReferences` section has been read.
         */
        template<typename T>
        void ReadExternalReference(const std::shared_ptr<ExternalReference<T>>& existingInstance)
        {
            std::shared_ptr<ExternalReference<T>> target = existingInstance;
            ReadExternalReferenceCore(ContentTypeName<T>::Name(),
                                      [target](std::string filename) { target->setFilenameProperty(std::move(filename)); });
        }

        /**
         * @brief Reads a single object from the input XML stream.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadObject(const ContentSerializerAttribute& format)
        {
            return FromContentObject<T>(ReadObjectCore(format, IntermediateSerializer::TypeSerializerFor<T>(), ContentObject{}));
        }

        /**
         * @brief Reads a single object from the input XML stream using the specified type hint.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @param typeSerializer The type serializer of the declared type.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadObject(const ContentSerializerAttribute& format,
                                            ContentTypeSerializerBase& typeSerializer)
        {
            return FromContentObject<T>(ReadObjectCore(format, typeSerializer, ContentObject{}));
        }

        /**
         * @brief Reads a single object from the input XML stream using the specified type hint and
         *        existing instance.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @param typeSerializer The type serializer of the declared type.
         * @param existingInstance The instance to fill.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadObject(const ContentSerializerAttribute& format,
                                            ContentTypeSerializerBase& typeSerializer, Carrier<T> existingInstance)
        {
            return FromContentObject<T>(ReadObjectCore(format, typeSerializer, ToContentObject<T>(existingInstance)));
        }

        /**
         * @brief Reads a single object from the input XML stream into an existing instance.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @param existingInstance The instance to fill.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadObject(const ContentSerializerAttribute& format, Carrier<T> existingInstance)
        {
            return FromContentObject<T>(
                ReadObjectCore(format, IntermediateSerializer::TypeSerializerFor<T>(), ToContentObject<T>(existingInstance)));
        }

        /**
         * @brief Reads a single object from the input XML stream, as the raw content of the
         *        element: no `Null` or `Type` attributes are examined.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadRawObject(const ContentSerializerAttribute& format)
        {
            return FromContentObject<T>(ReadRawObjectCore(format, IntermediateSerializer::TypeSerializerFor<T>(), ContentObject{}));
        }

        /**
         * @brief Reads a single raw object using the specified type serializer.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @param typeSerializer The type serializer.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadRawObject(const ContentSerializerAttribute& format,
                                               ContentTypeSerializerBase& typeSerializer)
        {
            return FromContentObject<T>(ReadRawObjectCore(format, typeSerializer, ContentObject{}));
        }

        /**
         * @brief Reads a single raw object using the specified type serializer and existing
         *        instance.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @param typeSerializer The type serializer.
         * @param existingInstance The instance to fill.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadRawObject(const ContentSerializerAttribute& format,
                                               ContentTypeSerializerBase& typeSerializer, Carrier<T> existingInstance)
        {
            return FromContentObject<T>(ReadRawObjectCore(format, typeSerializer, ToContentObject<T>(existingInstance)));
        }

        /**
         * @brief Reads a single raw object into an existing instance.
         *
         * @tparam T The declared type.
         * @param format The format of the XML.
         * @param existingInstance The instance to fill.
         * @return The object read.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> ReadRawObject(const ContentSerializerAttribute& format, Carrier<T> existingInstance)
        {
            return FromContentObject<T>(
                ReadRawObjectCore(format, IntermediateSerializer::TypeSerializerFor<T>(), ToContentObject<T>(existingInstance)));
        }

        /**
         * @brief Reads a shared resource ID and records it for subsequent operations.
         *
         * @tparam T The declared type of the resource.
         * @param format The format of the XML.
         * @param fixup The fixup operation to perform with the resource once the `Resources`
         *        section has been read. It is not invoked for an empty reference.
         */
        template<typename T>
        void ReadSharedResource(const ContentSerializerAttribute& format, std::function<void(Carrier<T>)> fixup)
        {
            ReadSharedResourceCore(format, IntermediateSerializer::TypeSerializerFor<T>(),
                                   [fixup](const ContentObject& value) { fixup(FromContentObject<T>(value)); });
        }

        /**
         * @brief Reads the `Type` attribute of the current element.
         *
         * @return The type it names.
         * @throws System::ArgumentException when the attribute names no registered type.
         */
        [[nodiscard]] System::Type ReadTypeName();

        /**
         * @brief Non-template form of `ReadObject`: consumes the member element (unless
         *        flattened), honours `Null` and `Type`, and lets the resolved serializer read.
         *
         * @param format The member's format.
         * @param declaredSerializer The declared type's serializer.
         * @param existingInstance The instance to fill, or an empty box.
         * @return The boxed value.
         */
        CNAEXT [[nodiscard]] ContentObject ReadObjectCore(const ContentSerializerAttribute& format,
                                                          ContentTypeSerializerBase& declaredSerializer,
                                                          const ContentObject& existingInstance);

        /**
         * @brief Non-template form of `ReadRawObject`.
         *
         * @param format The member's format.
         * @param typeSerializer The serializer that reads the element content.
         * @param existingInstance The instance to fill, or an empty box.
         * @return The boxed value.
         */
        CNAEXT [[nodiscard]] ContentObject ReadRawObjectCore(const ContentSerializerAttribute& format,
                                                             ContentTypeSerializerBase& typeSerializer,
                                                             const ContentObject& existingInstance);

        /**
         * @brief Non-template form of `ReadSharedResource`.
         *
         * @param format The member's format.
         * @param declaredSerializer The declared type's serializer, used to check the resource's type.
         * @param fixup Receives the boxed resource once it is known.
         */
        CNAEXT void ReadSharedResourceCore(const ContentSerializerAttribute& format,
                                           ContentTypeSerializerBase& declaredSerializer,
                                           std::function<void(const ContentObject&)> fixup);

        /**
         * @brief Non-template form of `ReadExternalReference`.
         *
         * @param targetTypeName The .NET full name of the referenced content type.
         * @param setFilename Receives the resolved filename once the section is known.
         */
        CNAEXT void ReadExternalReferenceCore(const std::string& targetTypeName,
                                              std::function<void(std::string)> setFilename);

        /**
         * @brief Reads the text of the member being read: the content of the current element, or
         *        the character data at the current position for a flattened member.
         *
         * @param format The member's format.
         * @return The text; empty for an empty element.
         */
        CNAEXT [[nodiscard]] std::string ReadText(const ContentSerializerAttribute& format);

        /**
         * @brief Reads the text content of the element whose start tag was just consumed, leaving
         *        the reader on its end tag.
         *
         * @return The text; empty for an empty element.
         * @throws System::Xml::XmlException when the element has child elements.
         */
        CNAEXT [[nodiscard]] std::string ReadElementText();

        /**
         * @brief Reads the character data at the current position (a flattened value).
         *
         * @return The text.
         * @throws InvalidContentException when the current node is an element.
         */
        CNAEXT [[nodiscard]] std::string ReadContentText();

        /**
         * @brief Consumes the end tag of the element being read, skipping comments and processing
         *        instructions.
         *
         * @throws System::Xml::XmlException when another node is found first.
         */
        CNAEXT void ReadEndElement();

        /**
         * @brief Tells whether the element being read was written in the empty-tag form, so it has
         *        no content to read.
         *
         * @return True inside `<X />`.
         */
        CNAEXT [[nodiscard]] bool CurrentElementIsEmpty() const noexcept;

        /** @brief Tells whether shared-resource references await the `Resources` section. */
        CNAEXT [[nodiscard]] bool HasPendingSharedFixups() const noexcept;

        /** @brief Tells whether external references await the `ExternalReferences` section. */
        CNAEXT [[nodiscard]] bool HasPendingExternalFixups() const noexcept;

        /**
         * @brief Splits packed text into whitespace-separated tokens.
         *
         * @param text The text.
         * @return The tokens.
         */
        CNAEXT [[nodiscard]] static std::vector<std::string> SplitTokens(std::string_view text);

        /**
         * @brief The reader's position as .NET spells it in messages.
         *
         * @return `Line L, position P.`
         */
        CNAEXT [[nodiscard]] std::string Location() const;

    private:
        friend class IntermediateSerializer;

        struct SharedFixup
        {
            std::string id;
            ContentTypeSerializerBase* declared;
            std::function<void(const ContentObject&)> apply;
        };

        struct ExternalFixup
        {
            std::string id;
            std::string targetTypeName;
            std::function<void(std::string)> apply;
        };

        struct SharedResource
        {
            ContentObject value;
            ContentTypeSerializerBase* serializer;
        };

        struct ExternalEntry
        {
            std::string targetTypeName;
            std::string filename;
        };

        IntermediateReader(IntermediateSerializer& serializer, System::Xml::XmlReader& xml);

        ContentObject ReadRootAsset(ContentTypeSerializerBase& serializer);
        void ReadResources();
        void ReadExternalReferences();
        void ExpectEndOfDocument();
        void ApplyFixups();
        ContentTypeSerializerBase& ResolveDeclared(const ContentSerializerAttribute& format,
                                                   ContentTypeSerializerBase& declaredSerializer, bool rootAsset);
        [[noreturn]] void ThrowInvalidNodeType() const;

        IntermediateSerializer& serializer_;
        System::Xml::XmlReader& xml_;
        std::vector<SharedFixup> sharedFixups_;
        std::vector<ExternalFixup> externalFixups_;
        std::vector<std::pair<std::string, SharedResource>> sharedResources_;
        std::vector<std::pair<std::string, ExternalEntry>> externalReferences_;
        bool currentElementEmpty_ = false;
    };
}
