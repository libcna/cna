// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "System/Object.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    class IntermediateReader;
    class IntermediateSerializer;
    class IntermediateWriter;

    /**
     * @brief Converts a typed carrier into the boxed form the serializers exchange; an `object`
     *        payload (a ContentObject) passes through unchanged.
     *
     * @tparam T The declared type.
     * @param value The carrier.
     * @return The boxed value.
     */
    template<typename T>
    [[nodiscard]] CNAEXT ContentObject ToContentObject(const Carrier<T>& value)
    {
        if constexpr (std::is_same_v<T, ContentObject>)
        {
            return value;
        }
        else
        {
            return Box<T>(value);
        }
    }

    /**
     * @brief Provides methods for serializing and deserializing a specific managed type.
     *
     * XNA spells the abstract base and the generic base with one name, `ContentTypeSerializer`
     * and `ContentTypeSerializer<T>`; C++ cannot give a class and a class template the same name,
     * so the non-generic base is `ContentTypeSerializerBase`, exactly as `ContentTypeWriterBase`
     * stands beside `ContentTypeWriter<T>`. The `protected internal` members of XNA are `protected`
     * here and reached by the reader, writer and serializer through the `Invoke*` entry points.
     */
    class ContentTypeSerializerBase
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer";

        /**
         * @brief Callback used when scanning the children of an object: receives the declared
         *        serializer of the child and the child's value.
         */
        using ChildCallback = std::function<void(ContentTypeSerializerBase& typeSerializer, const ContentObject& value)>;

        /** @brief Destroys the serializer. */
        virtual ~ContentTypeSerializerBase() = default;

        /**
         * @brief Gets a value indicating whether this component may load data into an existing
         *        object or if it must it construct a new instance of the object before loading the
         *        data.
         *
         * @return True when deserialization into an existing instance is possible; false by default.
         */
        [[nodiscard]] virtual bool getCanDeserializeIntoExistingObjectProperty() const;

        /**
         * @brief Gets the type handled by this serializer component.
         *
         * @return The target type.
         */
        [[nodiscard]] System::Type getTargetTypeProperty() const noexcept;

        /**
         * @brief Gets a short-form XML name for the target type, or an empty string if there is
         *        none.
         *
         * @return The XML type name (`int`, `string`, …), or empty.
         */
        [[nodiscard]] const std::string& getXmlTypeNameProperty() const noexcept;

        /**
         * @brief Queries whether an object contains data to be serialized.
         *
         * @param value The object to query.
         * @return True when the object is empty (an empty collection); false by default.
         */
        [[nodiscard]] virtual bool ObjectIsEmpty(const ContentObject& value) const;

        /**
         * @brief Returns the .NET full name of the target type (`ContentTypeName<T>::Name()`).
         *
         * @return The full type name.
         */
        CNAEXT [[nodiscard]] const std::string& TargetTypeName() const noexcept;

        /**
         * @brief Tells whether the target travels as a reference (`std::shared_ptr`) and so has
         *        identity and a dynamic type.
         *
         * @return True for reference carriers.
         */
        CNAEXT [[nodiscard]] virtual bool IsReferenceType() const noexcept;

        /**
         * @brief Tells whether the target can hold a null value (a reference or a nullable).
         *
         * @return True when `Null="true"` is a valid spelling of a value of this type.
         */
        CNAEXT [[nodiscard]] virtual bool IsNullable() const noexcept;

        /**
         * @brief Tells whether the target type cannot be instantiated.
         *
         * @return True for an abstract type.
         */
        CNAEXT [[nodiscard]] virtual bool IsAbstract() const noexcept;

        /**
         * @brief Tells whether a boxed value is null.
         *
         * @param value The boxed value.
         * @return True for an empty box, a null reference or an empty optional.
         */
        CNAEXT [[nodiscard]] virtual bool IsNull(const ContentObject& value) const;

        /**
         * @brief Returns the boxed null value of this type.
         *
         * @return A box holding a null reference or an empty optional.
         */
        CNAEXT [[nodiscard]] virtual ContentObject NullObject() const;

        /**
         * @brief Creates a default instance of the target type.
         *
         * @return The boxed new instance.
         * @throws InvalidContentException when the type is abstract or has no default constructor.
         */
        CNAEXT [[nodiscard]] virtual ContentObject CreateInstance() const;

        /**
         * @brief Views a boxed reference as a `System::Object` pointer, for dynamic-type dispatch
         *        and identity.
         *
         * @param value The boxed value.
         * @return The object pointer; null for value types and null references.
         */
        CNAEXT [[nodiscard]] virtual std::shared_ptr<System::Object> AsObject(const ContentObject& value) const;

        /**
         * @brief Boxes a `System::Object` pointer as this serializer's target type.
         *
         * @param value The object; must be an instance of the target type.
         * @return The boxed value, or an empty box when the object is not an instance of the type.
         */
        CNAEXT [[nodiscard]] virtual ContentObject FromObject(const std::shared_ptr<System::Object>& value) const;

        /**
         * @brief Returns the dynamic type of a boxed value: the most-derived type of a reference,
         *        the target type otherwise.
         *
         * @param value The boxed value.
         * @return The dynamic type.
         */
        CNAEXT [[nodiscard]] virtual System::Type DynamicType(const ContentObject& value) const;

        /**
         * @brief Number of whitespace-separated tokens one value takes when it is written as
         *        packed text, or 0 when values of this type are never packed.
         *
         * XNA packs collections of `bool`, the integer types, `float`, `double` and the
         * single-token framework value types (docs/xna-intermediate-xml-format.md §6).
         *
         * @return The token count, 0 for element-per-item types.
         */
        CNAEXT [[nodiscard]] virtual std::size_t PackedTokenCount() const noexcept;

        /**
         * @brief Formats a value as its packed tokens.
         *
         * @param value The boxed value.
         * @return The whitespace-separated tokens.
         */
        CNAEXT [[nodiscard]] virtual std::string FormatPacked(const ContentObject& value) const;

        /**
         * @brief Parses exactly `PackedTokenCount()` tokens into a value.
         *
         * @param tokens The tokens.
         * @return The boxed value.
         */
        CNAEXT [[nodiscard]] virtual ContentObject ParsePacked(std::span<const std::string> tokens) const;

        /**
         * @brief The C++ type a boxed value of this serializer's target holds: the carrier.
         *
         * @return `typeid` of the carrier; the target type itself for the base class.
         */
        CNAEXT [[nodiscard]] virtual std::type_index CarrierType() const;

        /**
         * @brief The identity of a boxed reference, for shared-resource bookkeeping.
         *
         * @param value The boxed value.
         * @return A pointer unique to the instance; null for value types and null references.
         */
        CNAEXT [[nodiscard]] virtual const void* Identity(const ContentObject& value) const;

        /**
         * @brief For a nullable: the serializer of the underlying type, which a boxed value belongs
         *        to.
         *
         * @return The underlying serializer, or null for every other type.
         */
        CNAEXT [[nodiscard]] virtual const ContentTypeSerializerBase* UnderlyingSerializer() const;

        /**
         * @brief For a nullable: the boxed underlying value.
         *
         * @param value The boxed nullable.
         * @return The boxed value, or an empty box when it has none.
         */
        CNAEXT [[nodiscard]] virtual ContentObject UnderlyingValue(const ContentObject& value) const;

        /**
         * @brief For a reference wrapper of a value type: wraps a boxed value in a shared instance.
         *
         * @param value The boxed value.
         * @return The wrapped box, or an empty box when this serializer does not wrap.
         */
        CNAEXT [[nodiscard]] virtual ContentObject WrapValue(const ContentObject& value) const;

        /**
         * @brief The item element name a collection of this type declares through
         *        `[ContentSerializerCollectionItemName]`, or empty for the default.
         *
         * @return The declared item name or empty.
         */
        CNAEXT [[nodiscard]] virtual std::string CollectionItemName() const;

        /**
         * @brief Invokes the protected `Initialize` -- the C++ form of XNA's `protected internal`.
         *
         * @param serializer The serializer that owns this component.
         */
        CNAEXT void InvokeInitialize(IntermediateSerializer& serializer) { Initialize(serializer); }

        /**
         * @brief Invokes the protected `ScanChildren`.
         *
         * @param serializer The serializer performing the scan.
         * @param callback Called for every child.
         * @param value The object whose children are scanned.
         */
        CNAEXT void InvokeScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                                       const ContentObject& value)
        {
            ScanChildren(serializer, callback, value);
        }

        /**
         * @brief Invokes the protected `Serialize`.
         *
         * @param output The writer.
         * @param value The boxed value.
         * @param format The member's format attribute.
         */
        CNAEXT void InvokeSerialize(IntermediateWriter& output, const ContentObject& value,
                                    const ContentSerializerAttribute& format)
        {
            Serialize(output, value, format);
        }

        /**
         * @brief Invokes the protected `Deserialize`.
         *
         * @param input The reader.
         * @param format The member's format attribute.
         * @param existingInstance The instance to fill, or an empty box.
         * @return The deserialized boxed value.
         */
        CNAEXT [[nodiscard]] ContentObject InvokeDeserialize(IntermediateReader& input,
                                                             const ContentSerializerAttribute& format,
                                                             const ContentObject& existingInstance)
        {
            return Deserialize(input, format, existingInstance);
        }

    protected:
        /**
         * @brief Initializes a new instance of the ContentTypeSerializer class.
         *
         * @param targetType The target type.
         */
        explicit ContentTypeSerializerBase(System::Type targetType);

        /**
         * @brief Initializes a new instance of the ContentTypeSerializer class using the specified
         *        XML shortcut name.
         *
         * @param targetType The target type.
         * @param xmlTypeName The XML shortcut name (`int`, `string`, …).
         */
        ContentTypeSerializerBase(System::Type targetType, std::string xmlTypeName);

        /**
         * @brief Initializes a serializer that also knows its target's .NET full name.
         *
         * @param targetType The target type.
         * @param xmlTypeName The XML shortcut name, or empty.
         * @param targetTypeName The .NET full name of the target type.
         */
        CNAEXT ContentTypeSerializerBase(System::Type targetType, std::string xmlTypeName, std::string targetTypeName);

        /**
         * @brief Retrieves and caches nested type serializers and allows deferred reading of
         *        type-specific settings; called once when the serializer is registered.
         *
         * @param serializer The content serializer.
         */
        virtual void Initialize(IntermediateSerializer& serializer);

        /**
         * @brief Examines the children of the specified object, passing each to a callback
         *        delegate.
         *
         * @param serializer The content serializer.
         * @param callback Delegate invoked for each child with its declared serializer and value.
         * @param value The object whose children are examined.
         */
        virtual void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                                  const ContentObject& value);

        /**
         * @brief Serializes an object to intermediate XML format.
         *
         * @param output Specifies the intermediate XML location, and provides various serialization
         *        helpers.
         * @param value The boxed value to be serialized.
         * @param format Specifies the content format for this object.
         */
        virtual void Serialize(IntermediateWriter& output, const ContentObject& value,
                               const ContentSerializerAttribute& format) = 0;

        /**
         * @brief Deserializes an object from intermediate XML format.
         *
         * @param input Location of the intermediate XML and various deserialization helpers.
         * @param format Specifies the intermediate source XML format.
         * @param existingInstance The object receiving the deserialized data, or an empty box.
         * @return The deserialized boxed object.
         */
        [[nodiscard]] virtual ContentObject Deserialize(IntermediateReader& input,
                                                        const ContentSerializerAttribute& format,
                                                        const ContentObject& existingInstance) = 0;

    private:
        System::Type targetType_;
        std::string xmlTypeName_;
        std::string targetTypeName_;
    };

    namespace detail
    {
        /** @brief True when @p T is carried by `std::shared_ptr` (a reference type). */
        template<typename T>
        inline constexpr bool IsReferenceCarrier = !std::is_same_v<Carrier<T>, T>;

        template<typename T>
        struct IsOptional : std::false_type
        {
        };

        template<typename T>
        struct IsOptional<std::optional<T>> : std::true_type
        {
        };

        template<typename T>
        struct IsSharedPtr : std::false_type
        {
        };

        template<typename T>
        struct IsSharedPtr<std::shared_ptr<T>> : std::true_type
        {
        };
    }

    /**
     * @brief Provides a generic implementation of ContentTypeSerializer methods and properties for
     *        serializing and deserializing a specific managed type.
     *
     * Derive from this class, override the typed `Serialize` and `Deserialize`, and register the
     * class with `IntermediateSerializer::AddTypeSerializer<TSerializer>()`. A reference-typed
     * @p T arrives as `std::shared_ptr<T>`, a value type by value.
     *
     * @tparam T The target type.
     */
    template<typename T>
    class ContentTypeSerializer : public ContentTypeSerializerBase
    {
    public:
        /** @brief The content type this serializer handles. */
        using TargetContentType = T;

        /** @brief The carrier the typed members receive. */
        using TargetCarrier = Carrier<T>;

        /**
         * @brief Queries whether an object contains data to be serialized.
         *
         * @param value The boxed object.
         * @return The answer of the typed overload.
         */
        [[nodiscard]] bool ObjectIsEmpty(const ContentObject& value) const override
        {
            return ObjectIsEmpty(Unbox<T>(value));
        }

        /**
         * @brief Queries whether a strongly typed object contains data to be serialized.
         *
         * @param value The object.
         * @return False unless overridden.
         */
        [[nodiscard]] virtual bool ObjectIsEmpty(const TargetCarrier& value) const
        {
            (void)value;
            return false;
        }

        /** @brief The carrier's `typeid`. */
        CNAEXT [[nodiscard]] std::type_index CarrierType() const override { return std::type_index(typeid(TargetCarrier)); }

        /** @brief Reference carriers have identity and a dynamic type. */
        CNAEXT [[nodiscard]] bool IsReferenceType() const noexcept override { return detail::IsReferenceCarrier<T>; }

        /** @brief References and optionals can be null. */
        CNAEXT [[nodiscard]] bool IsNullable() const noexcept override
        {
            return detail::IsReferenceCarrier<T> || detail::IsOptional<T>::value;
        }

        /** @brief An abstract class cannot be instantiated. */
        CNAEXT [[nodiscard]] bool IsAbstract() const noexcept override { return std::is_abstract_v<T>; }

        /** @brief Null for an empty box, a null pointer or an empty optional. */
        CNAEXT [[nodiscard]] bool IsNull(const ContentObject& value) const override
        {
            if (value.Empty())
            {
                return true;
            }
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                return Unbox<T>(value) == nullptr;
            }
            else if constexpr (detail::IsOptional<T>::value)
            {
                return !Unbox<T>(value).has_value();
            }
            else
            {
                return false;
            }
        }

        /** @brief The boxed null of this type. */
        CNAEXT [[nodiscard]] ContentObject NullObject() const override
        {
            if constexpr (detail::IsReferenceCarrier<T> || detail::IsOptional<T>::value)
            {
                return Box<T>(TargetCarrier{});
            }
            else
            {
                return ContentObject{};
            }
        }

        /** @brief A default-constructed instance, or a refusal for abstract types. */
        CNAEXT [[nodiscard]] ContentObject CreateInstance() const override
        {
            if constexpr (std::is_abstract_v<T>)
            {
                ThrowCannotCreate(true);
                return ContentObject{};
            }
            else if constexpr (detail::IsReferenceCarrier<T>)
            {
                if constexpr (std::is_default_constructible_v<T>)
                {
                    return Box<T>(std::make_shared<T>());
                }
                else
                {
                    ThrowCannotCreate(false);
                    return ContentObject{};
                }
            }
            else if constexpr (std::is_default_constructible_v<T>)
            {
                return Box<T>(T{});
            }
            else
            {
                ThrowCannotCreate(false);
                return ContentObject{};
            }
        }

        /** @brief The object pointer behind a reference carrier. */
        CNAEXT [[nodiscard]] std::shared_ptr<System::Object> AsObject(const ContentObject& value) const override
        {
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                if (value.Empty())
                {
                    return nullptr;
                }
                return std::static_pointer_cast<System::Object>(Unbox<T>(value));
            }
            else
            {
                (void)value;
                return nullptr;
            }
        }

        /** @brief Boxes an object pointer as `T`, when it is a `T`. */
        CNAEXT [[nodiscard]] ContentObject FromObject(const std::shared_ptr<System::Object>& value) const override
        {
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                auto typed = std::dynamic_pointer_cast<T>(value);
                if (typed == nullptr && value != nullptr)
                {
                    return ContentObject{};
                }
                return Box<T>(std::move(typed));
            }
            else
            {
                (void)value;
                return ContentObject{};
            }
        }

        /** @brief The most-derived type of a reference, the target type otherwise. */
        CNAEXT [[nodiscard]] System::Type DynamicType(const ContentObject& value) const override
        {
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                if (value.Empty())
                {
                    return getTargetTypeProperty();
                }
                const TargetCarrier& pointer = Unbox<T>(value);
                if (pointer == nullptr)
                {
                    return getTargetTypeProperty();
                }
                return System::Type::FromTypeInfo(typeid(*pointer));
            }
            else
            {
                (void)value;
                return getTargetTypeProperty();
            }
        }

    protected:
        /** @brief Initializes a new instance of the ContentTypeSerializer class. */
        ContentTypeSerializer()
            : ContentTypeSerializerBase(System::Type::From<T>(), std::string(), ContentTypeName<T>::Name())
        {
        }

        /**
         * @brief Initializes a new instance of the ContentTypeSerializer class using the specified
         *        XML shortcut name.
         *
         * @param xmlTypeName The XML shortcut name.
         */
        explicit ContentTypeSerializer(std::string xmlTypeName)
            : ContentTypeSerializerBase(System::Type::From<T>(), std::move(xmlTypeName), ContentTypeName<T>::Name())
        {
        }

        /**
         * @brief Serializes a strongly typed object to intermediate XML format.
         *
         * @param output The intermediate writer.
         * @param value The value to be serialized.
         * @param format Specifies the content format for this object.
         */
        virtual void Serialize(IntermediateWriter& output, const TargetCarrier& value,
                               const ContentSerializerAttribute& format) = 0;

        /**
         * @brief Deserializes a strongly typed object from intermediate XML format.
         *
         * @param input The intermediate reader.
         * @param format Specifies the intermediate source XML format.
         * @param existingInstance The instance receiving the data, or a default carrier.
         * @return The deserialized value.
         */
        [[nodiscard]] virtual TargetCarrier Deserialize(IntermediateReader& input,
                                                        const ContentSerializerAttribute& format,
                                                        TargetCarrier existingInstance) = 0;

        /**
         * @brief Examines the children of a strongly typed object.
         *
         * @param serializer The content serializer.
         * @param callback Delegate invoked for each child.
         * @param value The object whose children are examined.
         */
        virtual void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                                  const TargetCarrier& value)
        {
            (void)serializer;
            (void)callback;
            (void)value;
        }

        /** @brief Unboxes and forwards to the typed `Serialize`. */
        void Serialize(IntermediateWriter& output, const ContentObject& value,
                       const ContentSerializerAttribute& format) override
        {
            Serialize(output, Unbox<T>(value), format);
        }

        /** @brief Unboxes and forwards to the typed `Deserialize`, boxing the result. */
        [[nodiscard]] ContentObject Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                const ContentObject& existingInstance) override
        {
            TargetCarrier existing = existingInstance.Empty() ? TargetCarrier{} : Unbox<T>(existingInstance);
            return Box<T>(Deserialize(input, format, std::move(existing)));
        }

        /** @brief Unboxes and forwards to the typed `ScanChildren`. */
        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const ContentObject& value) override
        {
            ScanChildren(serializer, callback, Unbox<T>(value));
        }

    private:
        [[noreturn]] void ThrowCannotCreate(bool abstractType) const;
    };

    /**
     * @brief Throws the refusal `CreateInstance()` gives for a type it cannot construct.
     *
     * @param typeName The .NET full name of the type.
     * @param abstractType True when the type is abstract.
     */
    CNAEXT [[noreturn]] void ThrowCannotCreateInstance(const std::string& typeName, bool abstractType);

    template<typename T>
    void ContentTypeSerializer<T>::ThrowCannotCreate(bool abstractType) const
    {
        ThrowCannotCreateInstance(TargetTypeName(), abstractType);
    }
}
