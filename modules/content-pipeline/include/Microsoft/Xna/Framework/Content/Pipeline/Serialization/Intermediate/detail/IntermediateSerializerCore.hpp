// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <string_view>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializerAttribute.hpp"
#include "System/InvalidCastException.hpp"
#include "System/Type.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    class IntermediateReader;
    class IntermediateWriter;

    template<typename T>
    [[nodiscard]] CNAEXT Carrier<T> FromContentObject(const ContentObject& value);

    namespace detail
    {
        /**
         * @brief Creates and registers the serializer for a type the registry does not know yet:
         *        collections, nullables, references, enums and described types. Specialized in
         *        BuiltInTypeSerializers.hpp.
         */
        template<typename T>
        struct TypeSerializerFactory;
    }

    /**
     * @brief Provides methods for reading and writing XNA intermediate XML format.
     *
     * The static `Serialize` and `Deserialize` are XNA's public surface; an instance exists for
     * the duration of one call and is what `IntermediateReader::Serializer` and
     * `IntermediateWriter::Serializer` answer. The type-serializer registry is process-wide:
     * XNA finds `ContentTypeSerializer` classes by reflection, CNA by registration
     * (`AddTypeSerializer`, or automatically for a described type on first use).
     */
    class IntermediateSerializer final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer";

        /**
         * @brief Serializes an object into an intermediate XML file.
         *
         * @tparam T The declared type of the value.
         * @param output The XML writer that receives the document.
         * @param value The object to be serialized.
         * @param referenceRelocationPath Final name of the output file, used to relative encode
         *        external reference filenames; empty writes them as given.
         */
        template<typename T>
        static void Serialize(System::Xml::XmlWriter& output, const Carrier<T>& value,
                              const std::string& referenceRelocationPath)
        {
            SerializeObject(output, ToContentObject<T>(value), TypeSerializerFor<T>(), referenceRelocationPath);
        }

        /**
         * @brief Deserializes an intermediate XML file.
         *
         * @tparam T The declared type of the root object.
         * @param input The XML reader positioned at the start of the document.
         * @param referenceRelocationPath Final name of the output file, used to relative encode
         *        external reference filenames.
         * @return The deserialized object.
         * @throws InvalidContentException when the document is not intermediate XML of @p T.
         */
        template<typename T>
        [[nodiscard]] static Carrier<T> Deserialize(System::Xml::XmlReader& input,
                                                    const std::string& referenceRelocationPath)
        {
            return FromContentObject<T>(DeserializeObject(input, TypeSerializerFor<T>(), referenceRelocationPath));
        }

        /**
         * @brief Retrieves the worker serializer for a specified type.
         *
         * @param type The type.
         * @return The serializer.
         * @throws System::ArgumentException when no serializer is registered for the type.
         */
        [[nodiscard]] ContentTypeSerializerBase& GetTypeSerializer(System::Type type) const;

        /**
         * @brief Registers a user-defined `ContentTypeSerializer<T>`, as XNA does for a class
         *        marked `[ContentTypeSerializer]`.
         *
         * @tparam TSerializer The serializer class; default-constructible.
         * @param attribute The marker attribute (carries no settings in XNA 4.0).
         * @return The registered instance.
         */
        template<typename TSerializer>
        CNAEXT static ContentTypeSerializerBase& AddTypeSerializer(const ContentTypeSerializerAttribute& attribute = {})
        {
            (void)attribute;
            return RegisterTypeSerializer(std::make_unique<TSerializer>());
        }

        /**
         * @brief Returns the serializer for @p T, creating and registering it on first use.
         *
         * @tparam T The declared type.
         * @return The serializer.
         */
        template<typename T>
        CNAEXT static ContentTypeSerializerBase& TypeSerializerFor()
        {
            EnsureBuiltInTypeSerializers();
            if (ContentTypeSerializerBase* found = FindTypeSerializer(System::Type::From<T>()))
            {
                return *found;
            }
            return detail::TypeSerializerFactory<T>::Create();
        }

        /**
         * @brief Finds a registered serializer by target type.
         *
         * @param type The type.
         * @return The serializer, or null.
         */
        CNAEXT [[nodiscard]] static ContentTypeSerializerBase* FindTypeSerializer(System::Type type) noexcept;

        /**
         * @brief Finds the serializer whose carrier is the given C++ type.
         *
         * @param carrier The `typeid` of the carrier (`std::shared_ptr<T>` for a reference type).
         * @return The serializer, or null.
         */
        CNAEXT [[nodiscard]] static ContentTypeSerializerBase* FindTypeSerializerForCarrier(std::type_index carrier) noexcept;

        /**
         * @brief Finds the serializer of a boxed value: by its carrier type first, then by its
         *        stable type name.
         *
         * @param value The boxed value.
         * @return The serializer, or null for an empty box or an unknown type.
         */
        CNAEXT [[nodiscard]] static ContentTypeSerializerBase* FindTypeSerializer(const ContentObject& value) noexcept;

        /**
         * @brief Finds a registered serializer by name: the .NET full name, its canonical generic
         *        spelling, an XML shortcut (`int`) or a registered alias (`System.Int32[]`).
         *
         * @param typeName The name.
         * @return The serializer, or null.
         */
        CNAEXT [[nodiscard]] static ContentTypeSerializerBase* FindTypeSerializer(const std::string& typeName) noexcept;

        /**
         * @brief Registers a serializer under its target type, its .NET full name, its XML
         *        shortcut name and any additional names.
         *
         * @param serializer The serializer; ownership passes to the registry.
         * @param additionalNames Further names that resolve to it.
         * @param registerName False to register by type only (a reference wrapper of a value type
         *        must not shadow the value type's name).
         * @return The registered instance.
         */
        CNAEXT static ContentTypeSerializerBase& RegisterTypeSerializer(std::unique_ptr<ContentTypeSerializerBase> serializer,
                                                                        std::vector<std::string> additionalNames = {},
                                                                        bool registerName = true);

        /**
         * @brief Returns the registered serializer for a type, refusing when there is none.
         *
         * @param type The type.
         * @param typeName The .NET full name, for the message.
         * @return The serializer.
         * @throws System::ArgumentException when nothing is registered for the type.
         */
        CNAEXT [[nodiscard]] static ContentTypeSerializerBase& RequireTypeSerializer(System::Type type,
                                                                                    const std::string& typeName);

        /**
         * @brief Records a type name that may appear as an external reference's `TargetType`
         *        without having a serializer of its own.
         *
         * @param typeName The .NET full name.
         */
        CNAEXT static void RegisterKnownTypeName(const std::string& typeName);

        /**
         * @brief Tells whether a name is a registered serializer's or a known type name.
         *
         * @param typeName The .NET full name.
         * @return True when known.
         */
        CNAEXT [[nodiscard]] static bool IsKnownTypeName(const std::string& typeName) noexcept;

        /**
         * @brief Normalizes a .NET type name to the registry's key: generic arity markers removed,
         *        nested brackets flattened (`List`1[[System.Int32]]` becomes
         *        `List[System.Int32]`), assembly qualification dropped.
         *
         * @param name The name.
         * @return The canonical key.
         */
        CNAEXT [[nodiscard]] static std::string CanonicalTypeName(std::string_view name);

        /**
         * @brief Registers the built-in serializers (primitives, framework value types, object)
         *        once.
         */
        CNAEXT static void EnsureBuiltInTypeSerializers();

        /**
         * @brief The relocation path this serialization pass was given.
         *
         * @return The path, or empty.
         */
        CNAEXT [[nodiscard]] const std::string& getReferenceRelocationPathProperty() const noexcept;

        /**
         * @brief Spells a type for a `Type` attribute: the XML shortcut, the full `System` name,
         *        or `Alias:Name` with the alias recorded for the root's `xmlns` declarations.
         *
         * @param serializer The type's serializer.
         * @return The spelling.
         */
        CNAEXT [[nodiscard]] std::string SpellTypeName(const ContentTypeSerializerBase& serializer);

        /**
         * @brief Spells a .NET type name the way the external-reference section does: with a
         *        namespace alias when the document already declares one, and in full otherwise.
         *
         * That section is written after the root element, so it cannot declare a new alias, and
         * the runtime does not (measured, docs/xna-intermediate-xml-format.md §8).
         *
         * @param typeName The .NET full name of the type.
         * @return The spelled name.
         */
        CNAEXT [[nodiscard]] std::string SpellDeclaredTypeName(const std::string& typeName) const;

        /**
         * @brief Resolves a `Type` attribute value in the scope of the reader's current node.
         *
         * @param spelledName The attribute text.
         * @param scope The reader whose `xmlns` scope resolves prefixes.
         * @return The serializer.
         * @throws System::ArgumentException `Cannot find type "X"` when nothing is registered
         *         under the name, or `XML contains invalid type name …` for a malformed one.
         */
        CNAEXT [[nodiscard]] ContentTypeSerializerBase& ResolveTypeName(const std::string& spelledName,
                                                                        const System::Xml::XmlReader& scope);

        /**
         * @brief The namespace aliases recorded so far, in first-use order.
         *
         * @return Pairs of alias and namespace.
         */
        CNAEXT [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& NamespaceAliases() const noexcept;

        /**
         * @brief Non-template form of `Serialize`.
         *
         * @param output The XML writer.
         * @param value The boxed root object.
         * @param serializer The declared type's serializer.
         * @param referenceRelocationPath The relocation path, or empty.
         */
        CNAEXT static void SerializeObject(System::Xml::XmlWriter& output, const ContentObject& value,
                                           ContentTypeSerializerBase& serializer,
                                           const std::string& referenceRelocationPath);

        /**
         * @brief Non-template form of `Deserialize`.
         *
         * @param input The XML reader.
         * @param serializer The declared type's serializer.
         * @param referenceRelocationPath The relocation path, or empty.
         * @return The boxed root object.
         */
        CNAEXT [[nodiscard]] static ContentObject DeserializeObject(System::Xml::XmlReader& input,
                                                                    ContentTypeSerializerBase& serializer,
                                                                    const std::string& referenceRelocationPath);

    private:
        friend class IntermediateReader;
        friend class IntermediateWriter;

        explicit IntermediateSerializer(std::string referenceRelocationPath);

        std::string referenceRelocationPath_;
        std::vector<std::pair<std::string, std::string>> aliases_;
    };

    /**
     * @brief Converts a boxed value back to the carrier of a declared type, following a reference
     *        to its base class when the box holds a derived instance.
     *
     * @tparam T The declared type.
     * @param value The boxed value.
     * @return The carrier.
     * @throws System::InvalidCastException when the box holds something else.
     */
    template<typename T>
    Carrier<T> FromContentObject(const ContentObject& value)
    {
        if constexpr (std::is_same_v<T, ContentObject>)
        {
            return value;
        }
        else if constexpr (detail::IsReferenceCarrier<T>)
        {
            if (value.Empty())
            {
                return nullptr;
            }
            if (Holds<T>(value))
            {
                return Unbox<T>(value);
            }
            if (ContentTypeSerializerBase* actual = IntermediateSerializer::FindTypeSerializer(value.StableType()))
            {
                if (auto typed = std::dynamic_pointer_cast<T>(actual->AsObject(value)))
                {
                    return typed;
                }
            }
            throw System::InvalidCastException("Cannot convert content of type '" + value.StableType() + "' to '" +
                                               ContentTypeName<T>::Name() + "'.");
        }
        else if constexpr (detail::IsSharedPtr<T>::value)
        {
            using Pointee = typename T::element_type;
            if (value.Empty())
            {
                return nullptr;
            }
            if (value.CppType() == std::type_index(typeid(T)))
            {
                return value.Get<T>();
            }
            // A value-typed resource referenced through a reference wrapper. A type that cannot be
            // copied is never stored that way -- a NodeContent owns its children and is only ever
            // boxed as a pointer -- so that branch would be dead code that fails to compile.
            if constexpr (std::is_copy_constructible_v<Pointee>)
            {
                return std::make_shared<Pointee>(value.Get<Pointee>());
            }
            else
            {
                throw System::InvalidCastException("Cannot convert content of type '" + value.StableType() +
                                                   "' to '" + ContentTypeName<T>::Name() + "'.");
            }
        }
        else if constexpr (detail::IsOptional<T>::value)
        {
            if (value.Empty())
            {
                return T{};
            }
            if (Holds<T>(value))
            {
                return Unbox<T>(value);
            }
            // A nullable boxes as its underlying value.
            return T{Unbox<typename T::value_type>(value)};
        }
        else
        {
            return Unbox<T>(value);
        }
    }
}
