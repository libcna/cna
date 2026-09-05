// SPDX-License-Identifier: MS-PL
#pragma once

#include <map>
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
#include "System/Xml/XmlWriter.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Provides an implementation of many of the methods of IntermediateSerializer including
     *        method related to writing intermediate XML.
     */
    class IntermediateWriter final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter";

        /**
         * @brief Gets the parent serializer.
         *
         * @return The serializer performing this pass.
         */
        [[nodiscard]] IntermediateSerializer& getSerializerProperty() const noexcept;

        /**
         * @brief Gets the XML output stream.
         *
         * @return The writer.
         */
        [[nodiscard]] System::Xml::XmlWriter& getXmlProperty() const noexcept;

        /**
         * @brief Writes an external reference ID into the current element and records the
         *        filename for the `ExternalReferences` section.
         *
         * @tparam T The type of the referenced content.
         * @param value The external reference.
         */
        template<typename T>
        void WriteExternalReference(const ExternalReference<T>& value)
        {
            WriteExternalReferenceCore(value.getFilenameProperty(), ContentTypeName<T>::Name());
        }

        /**
         * @brief Writes a single object to the output XML stream.
         *
         * @tparam T The declared type.
         * @param value The object to write.
         * @param format The format of the XML.
         */
        template<typename T>
        void WriteObject(const Carrier<T>& value, const ContentSerializerAttribute& format)
        {
            WriteObjectCore(ToContentObject<T>(value), format, IntermediateSerializer::TypeSerializerFor<T>(), false);
        }

        /**
         * @brief Writes a single object to the output XML stream using the specified type hint.
         *
         * @tparam T The declared type.
         * @param value The object to write.
         * @param format The format of the XML.
         * @param typeSerializer The type serializer of the declared type.
         */
        template<typename T>
        void WriteObject(const Carrier<T>& value, const ContentSerializerAttribute& format,
                         ContentTypeSerializerBase& typeSerializer)
        {
            WriteObjectCore(ToContentObject<T>(value), format, typeSerializer, false);
        }

        /**
         * @brief Writes a single object to the output XML stream as raw element content, without
         *        `Null` or `Type` attributes.
         *
         * @tparam T The declared type.
         * @param value The object to write.
         * @param format The format of the XML.
         */
        template<typename T>
        void WriteRawObject(const Carrier<T>& value, const ContentSerializerAttribute& format)
        {
            WriteRawObjectCore(ToContentObject<T>(value), format, IntermediateSerializer::TypeSerializerFor<T>());
        }

        /**
         * @brief Writes a single raw object using the specified type serializer.
         *
         * @tparam T The declared type.
         * @param value The object to write.
         * @param format The format of the XML.
         * @param typeSerializer The type serializer.
         */
        template<typename T>
        void WriteRawObject(const Carrier<T>& value, const ContentSerializerAttribute& format,
                            ContentTypeSerializerBase& typeSerializer)
        {
            WriteRawObjectCore(ToContentObject<T>(value), format, typeSerializer);
        }

        /**
         * @brief Adds a shared reference to the output XML and records the object to be
         *        serialized later in the `Resources` section.
         *
         * @tparam T The declared type of the resource.
         * @param value The resource; identity decides whether it is a new resource.
         * @param format The format of the XML.
         */
        template<typename T>
        void WriteSharedResource(const Carrier<T>& value, const ContentSerializerAttribute& format)
        {
            WriteSharedResourceCore(ToContentObject<T>(value), format, IntermediateSerializer::TypeSerializerFor<T>());
        }

        /**
         * @brief Writes the name of a type as the `Type` attribute of the current element.
         *
         * @param type The type.
         */
        void WriteTypeName(System::Type type);

        /**
         * @brief Non-template form of `WriteObject`.
         *
         * @param value The boxed value.
         * @param format The member's format.
         * @param declaredSerializer The declared type's serializer.
         * @param forceTypeAttribute True to write the `Type` attribute even when the dynamic type
         *        is the declared one (the root `Asset`).
         */
        CNAEXT void WriteObjectCore(const ContentObject& value, const ContentSerializerAttribute& format,
                                    ContentTypeSerializerBase& declaredSerializer, bool forceTypeAttribute);

        /**
         * @brief Non-template form of `WriteRawObject`.
         *
         * @param value The boxed value.
         * @param format The member's format.
         * @param typeSerializer The serializer that writes the content.
         */
        CNAEXT void WriteRawObjectCore(const ContentObject& value, const ContentSerializerAttribute& format,
                                       ContentTypeSerializerBase& typeSerializer);

        /**
         * @brief Non-template form of `WriteSharedResource`.
         *
         * @param value The boxed resource.
         * @param format The member's format.
         * @param declaredSerializer The declared type's serializer.
         */
        CNAEXT void WriteSharedResourceCore(const ContentObject& value, const ContentSerializerAttribute& format,
                                            ContentTypeSerializerBase& declaredSerializer);

        /**
         * @brief Non-template form of `WriteExternalReference`.
         *
         * @param filename The referenced file; empty writes nothing.
         * @param targetTypeName The .NET full name of the referenced content type.
         */
        CNAEXT void WriteExternalReferenceCore(const std::string& filename, const std::string& targetTypeName);

        /**
         * @brief Resolves the serializer that writes a value: the declared one for value types,
         *        the most-derived registered one for references and `object`.
         *
         * @param value The boxed value; replaced by the value re-boxed as the dynamic type.
         * @param declaredSerializer The declared type's serializer.
         * @return The dynamic serializer.
         */
        CNAEXT [[nodiscard]] ContentTypeSerializerBase& ResolveDynamic(ContentObject& value,
                                                                       ContentTypeSerializerBase& declaredSerializer) const;

    private:
        friend class IntermediateSerializer;

        struct QueuedResource
        {
            std::string id;
            ContentObject value;
            ContentTypeSerializerBase* serializer;
        };

        struct QueuedExternal
        {
            std::string id;
            std::string targetTypeName;
            std::string filename;
        };

        IntermediateWriter(IntermediateSerializer& serializer, System::Xml::XmlWriter& xml);

        void ScanForNamespaces(const ContentObject& value, ContentTypeSerializerBase& declaredSerializer);
        void WriteRootAsset(const ContentObject& value, ContentTypeSerializerBase& serializer);
        void WriteResources();
        void WriteExternalReferences();
        [[nodiscard]] std::string RelativeFilename(const std::string& filename) const;

        IntermediateSerializer& serializer_;
        System::Xml::XmlWriter& xml_;
        std::vector<QueuedResource> resources_;
        std::map<const void*, std::string> resourceIds_;
        std::vector<QueuedExternal> externals_;
        std::map<std::pair<std::string, std::string>, std::string> externalIds_;
    };
}
