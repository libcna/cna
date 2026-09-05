// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/detail/IntermediateSerializerCore.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Throws `XML contains invalid value "…" for enum …` for text no enum member matches.
     *
     * @param text The offending text.
     * @param enumTypeName The enum's .NET full name.
     */
    CNAEXT [[noreturn]] void ThrowInvalidEnumValue(const std::string& text, const std::string& enumTypeName);

    /**
     * @brief Throws `XML does not have enough entries in space-separated list.`
     */
    CNAEXT [[noreturn]] void ThrowNotEnoughPackedEntries();

    /**
     * @brief Throws `An item with the same key has already been added.`
     */
    CNAEXT [[noreturn]] void ThrowDuplicateDictionaryKey();

    /**
     * @brief Trims .NET whitespace from both ends.
     *
     * @param text The text.
     * @return The trimmed view.
     */
    CNAEXT [[nodiscard]] std::string_view TrimXmlWhitespace(std::string_view text);

    /**
     * @brief Serializes an enumeration by member name, as XNA does: `Happy`, `Cheese, Olives`,
     *        `None`, or the number when no name matches. Reading accepts names, numbers and
     *        comma-separated flag names, case-sensitively.
     *
     * @tparam E The enumeration; `ContentEnumNames<E>` must be specialized.
     */
    template<typename E>
    class CNAEXT EnumSerializer final : public ContentTypeSerializer<E>
    {
        static_assert(std::is_enum_v<E>, "EnumSerializer<E>: E must be an enumeration.");
        using Underlying = std::underlying_type_t<E>;
        using Names = ContentEnumNames<E>;

    public:
        /** @brief Creates the serializer. */
        EnumSerializer() = default;

        /**
         * @brief Formats a value the way XNA writes it.
         *
         * @param value The value.
         * @return The member name, flag names, or number.
         */
        [[nodiscard]] static std::string Format(E value)
        {
            for (const auto& [member, name] : Names::Names)
            {
                if (member == value)
                {
                    return std::string(name);
                }
            }
            const Underlying raw = static_cast<Underlying>(value);
            if constexpr (Names::Flags)
            {
                Underlying remaining = raw;
                std::string out;
                for (const auto& [member, name] : Names::Names)
                {
                    const Underlying bits = static_cast<Underlying>(member);
                    if (bits != 0 && (remaining & bits) == bits)
                    {
                        if (!out.empty())
                        {
                            out += ", ";
                        }
                        out += name;
                        remaining = static_cast<Underlying>(remaining & ~bits);
                    }
                }
                if (remaining == 0 && !out.empty())
                {
                    return out;
                }
            }
            return std::to_string(raw);
        }

        /**
         * @brief Parses text the way XNA reads it.
         *
         * @param text The element text.
         * @return The value.
         * @throws InvalidContentException for text no member matches.
         */
        [[nodiscard]] E Parse(const std::string& text) const
        {
            const std::string_view trimmed = TrimXmlWhitespace(text);
            if (!trimmed.empty())
            {
                std::size_t i = (trimmed[0] == '-' || trimmed[0] == '+') ? 1 : 0;
                bool numeric = i < trimmed.size();
                for (; i < trimmed.size() && numeric; ++i)
                {
                    numeric = trimmed[i] >= '0' && trimmed[i] <= '9';
                }
                if (numeric)
                {
                    return static_cast<E>(static_cast<Underlying>(std::stoll(std::string(trimmed))));
                }
            }
            Underlying result = 0;
            std::size_t start = 0;
            bool any = false;
            while (start <= trimmed.size())
            {
                std::size_t comma = trimmed.find(',', start);
                if (comma == std::string_view::npos)
                {
                    comma = trimmed.size();
                }
                const std::string_view token = TrimXmlWhitespace(trimmed.substr(start, comma - start));
                bool found = false;
                for (const auto& [member, name] : Names::Names)
                {
                    if (name == token)
                    {
                        result = static_cast<Underlying>(result | static_cast<Underlying>(member));
                        found = true;
                        break;
                    }
                }
                if (!found || (any && !Names::Flags))
                {
                    ThrowInvalidEnumValue(text, this->TargetTypeName());
                }
                any = true;
                start = comma + 1;
                if (comma == trimmed.size())
                {
                    break;
                }
            }
            if (!any)
            {
                ThrowInvalidEnumValue(text, this->TargetTypeName());
            }
            return static_cast<E>(result);
        }

    protected:
        void Serialize(IntermediateWriter& output, const E& value, const ContentSerializerAttribute& format) override
        {
            (void)format;
            output.getXmlProperty().WriteString(Format(value));
        }

        [[nodiscard]] E Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                    E existingInstance) override
        {
            (void)existingInstance;
            return Parse(input.ReadText(format));
        }
    };

    /**
     * @brief Serializes `std::vector<T>` as XNA serializes `List<T>` and `T[]`: packed text for
     *        single-token element types, `<Item>` children otherwise.
     *
     * @tparam T The element type.
     */
    template<typename T>
    class CNAEXT ListSerializer final : public ContentTypeSerializer<std::vector<T>>
    {
        using Element = detail::DeclaredContentType<T>;
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        ListSerializer() = default;

        /** @brief Collections are filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

        /** @brief An empty collection has nothing to write. */
        [[nodiscard]] bool ObjectIsEmpty(const std::vector<T>& value) const override { return value.empty(); }

    protected:
        void Serialize(IntermediateWriter& output, const std::vector<T>& value,
                       const ContentSerializerAttribute& format) override
        {
            ContentTypeSerializerBase& element = IntermediateSerializer::TypeSerializerFor<Element>();
            if (element.PackedTokenCount() > 0)
            {
                std::string text;
                for (const T& item : value)
                {
                    if (!text.empty())
                    {
                        text += ' ';
                    }
                    text += element.FormatPacked(ToContentObject<Element>(item));
                }
                if (!text.empty())
                {
                    output.getXmlProperty().WriteString(text);
                }
                return;
            }
            ContentSerializerAttribute itemFormat;
            itemFormat.setElementNameProperty(format.getCollectionItemNameProperty());
            for (const T& item : value)
            {
                output.WriteObjectCore(ToContentObject<Element>(item), itemFormat, element, false);
            }
        }

        [[nodiscard]] std::vector<T> Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                 std::vector<T> existingInstance) override
        {
            ContentTypeSerializerBase& element = IntermediateSerializer::TypeSerializerFor<Element>();
            const std::size_t width = element.PackedTokenCount();
            if (width > 0)
            {
                const std::vector<std::string> tokens = IntermediateReader::SplitTokens(input.ReadContentText());
                for (std::size_t i = 0; i < tokens.size(); i += width)
                {
                    if (i + width > tokens.size())
                    {
                        ThrowNotEnoughPackedEntries();
                    }
                    existingInstance.push_back(FromContentObject<Element>(
                        element.ParsePacked(std::span<const std::string>(tokens.data() + i, width))));
                }
                return existingInstance;
            }
            ContentSerializerAttribute itemFormat;
            itemFormat.setElementNameProperty(format.getCollectionItemNameProperty());
            while (input.MoveToElement(itemFormat.getElementNameProperty()))
            {
                existingInstance.push_back(
                    FromContentObject<Element>(input.ReadObjectCore(itemFormat, element, ContentObject{})));
            }
            return existingInstance;
        }

        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const std::vector<T>& value) override
        {
            (void)serializer;
            ContentTypeSerializerBase& element = IntermediateSerializer::TypeSerializerFor<Element>();
            for (const T& item : value)
            {
                callback(element, ToContentObject<Element>(item));
            }
        }
    };

    /**
     * @brief Serializes `std::map<K, V>` as XNA serializes `Dictionary<K, V>`:
     *        `<Item><Key>…</Key><Value>…</Value></Item>` per entry, in key order.
     *
     * @tparam K The key type.
     * @tparam V The value type.
     */
    template<typename K, typename V>
    class CNAEXT DictionarySerializer final : public ContentTypeSerializer<std::map<K, V>>
    {
        using Key = detail::DeclaredContentType<K>;
        using Value = detail::DeclaredContentType<V>;
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        DictionarySerializer() = default;

        /** @brief Collections are filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

        /** @brief An empty collection has nothing to write. */
        [[nodiscard]] bool ObjectIsEmpty(const std::map<K, V>& value) const override { return value.empty(); }

    protected:
        void Serialize(IntermediateWriter& output, const std::map<K, V>& value,
                       const ContentSerializerAttribute& format) override
        {
            ContentTypeSerializerBase& keySerializer = IntermediateSerializer::TypeSerializerFor<Key>();
            ContentTypeSerializerBase& valueSerializer = IntermediateSerializer::TypeSerializerFor<Value>();
            ContentSerializerAttribute keyFormat;
            keyFormat.setElementNameProperty("Key");
            ContentSerializerAttribute valueFormat;
            valueFormat.setElementNameProperty("Value");
            for (const auto& [key, item] : value)
            {
                output.getXmlProperty().WriteStartElement(format.getCollectionItemNameProperty());
                output.WriteObjectCore(ToContentObject<Key>(key), keyFormat, keySerializer, false);
                output.WriteObjectCore(ToContentObject<Value>(item), valueFormat, valueSerializer, false);
                output.getXmlProperty().WriteEndElement();
            }
        }

        [[nodiscard]] std::map<K, V> Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                 std::map<K, V> existingInstance) override
        {
            ContentTypeSerializerBase& keySerializer = IntermediateSerializer::TypeSerializerFor<Key>();
            ContentTypeSerializerBase& valueSerializer = IntermediateSerializer::TypeSerializerFor<Value>();
            ContentSerializerAttribute keyFormat;
            keyFormat.setElementNameProperty("Key");
            ContentSerializerAttribute valueFormat;
            valueFormat.setElementNameProperty("Value");
            const std::string itemName = format.getCollectionItemNameProperty();
            while (input.MoveToElement(itemName))
            {
                const bool empty = input.getXmlProperty().getIsEmptyElementProperty();
                input.getXmlProperty().ReadStartElement();
                if (empty)
                {
                    ThrowNotEnoughPackedEntries();
                }
                K key = FromContentObject<Key>(input.ReadObjectCore(keyFormat, keySerializer, ContentObject{}));
                V item = FromContentObject<Value>(input.ReadObjectCore(valueFormat, valueSerializer, ContentObject{}));
                input.ReadEndElement();
                if (!existingInstance.emplace(std::move(key), std::move(item)).second)
                {
                    ThrowDuplicateDictionaryKey();
                }
            }
            return existingInstance;
        }

        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const std::map<K, V>& value) override
        {
            (void)serializer;
            ContentTypeSerializerBase& keySerializer = IntermediateSerializer::TypeSerializerFor<Key>();
            ContentTypeSerializerBase& valueSerializer = IntermediateSerializer::TypeSerializerFor<Value>();
            for (const auto& [key, item] : value)
            {
                callback(keySerializer, ToContentObject<Key>(key));
                callback(valueSerializer, ToContentObject<Value>(item));
            }
        }
    };

    /**
     * @brief Serializes `std::optional<T>` as XNA serializes `Nullable<T>`: the value's own
     *        text, or `Null="true"`. A boxed nullable is its value, so the `Type` attribute
     *        names `T`.
     *
     * @tparam T The underlying type.
     */
    template<typename T>
    class CNAEXT NullableSerializer final : public ContentTypeSerializer<std::optional<T>>
    {
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        NullableSerializer() = default;

        /** @brief The underlying type's serializer, which a boxed value belongs to. */
        [[nodiscard]] const ContentTypeSerializerBase* UnderlyingSerializer() const override
        {
            return &IntermediateSerializer::TypeSerializerFor<T>();
        }

        /** @brief The boxed underlying value. */
        [[nodiscard]] ContentObject UnderlyingValue(const ContentObject& value) const override
        {
            const std::optional<T>& optional = Unbox<std::optional<T>>(value);
            return optional.has_value() ? Box<T>(*optional) : ContentObject{};
        }

    protected:
        void Serialize(IntermediateWriter& output, const std::optional<T>& value,
                       const ContentSerializerAttribute& format) override
        {
            IntermediateSerializer::TypeSerializerFor<T>().InvokeSerialize(output, Box<T>(*value), format);
        }

        [[nodiscard]] std::optional<T> Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                   std::optional<T> existingInstance) override
        {
            (void)existingInstance;
            return std::optional<T>(
                Unbox<T>(IntermediateSerializer::TypeSerializerFor<T>().InvokeDeserialize(input, format, ContentObject{})));
        }

        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const std::optional<T>& value) override
        {
            (void)serializer;
            if (value.has_value())
            {
                callback(IntermediateSerializer::TypeSerializerFor<T>(), Box<T>(*value));
            }
        }
    };

    /**
     * @brief Gives a value type reference semantics through `std::shared_ptr<U>`: identity for
     *        shared resources, null, and filling in place. Registered by type only, so the type's
     *        name still resolves to the value serializer.
     *
     * @tparam U A type that is not derived from `System::Object`.
     */
    template<typename U>
    class CNAEXT ReferenceSerializer final : public ContentTypeSerializer<std::shared_ptr<U>>
    {
        static_assert(!std::is_base_of_v<System::Object, U>,
                      "ReferenceSerializer<U>: a System::Object-derived type already travels by reference.");
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;

    public:
        /** @brief Creates the serializer. */
        ReferenceSerializer() = default;

        /** @brief A reference. */
        [[nodiscard]] bool IsReferenceType() const noexcept override { return true; }

        /** @brief Can be null. */
        [[nodiscard]] bool IsNullable() const noexcept override { return true; }

        /** @brief Null for an empty box or a null pointer. */
        [[nodiscard]] bool IsNull(const ContentObject& value) const override
        {
            return value.Empty() || Unbox<std::shared_ptr<U>>(value) == nullptr;
        }

        /** @brief A box holding a null pointer. */
        [[nodiscard]] ContentObject NullObject() const override { return Box<std::shared_ptr<U>>(nullptr); }

        /** @brief The pointer's identity. */
        [[nodiscard]] const void* Identity(const ContentObject& value) const override
        {
            return value.Empty() ? nullptr : Unbox<std::shared_ptr<U>>(value).get();
        }

        /** @brief As the wrapped value serializer answers. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override
        {
            return IntermediateSerializer::TypeSerializerFor<U>().getCanDeserializeIntoExistingObjectProperty();
        }

        /** @brief Wraps a boxed value in a fresh shared instance. */
        [[nodiscard]] ContentObject WrapValue(const ContentObject& value) const override
        {
            if (value.CppType() == std::type_index(typeid(std::shared_ptr<U>)))
            {
                return value;
            }
            return Box<std::shared_ptr<U>>(std::make_shared<U>(Unbox<U>(value)));
        }

        /** @brief Empty as the wrapped value is. */
        [[nodiscard]] bool ObjectIsEmpty(const std::shared_ptr<U>& value) const override
        {
            return value != nullptr && IntermediateSerializer::TypeSerializerFor<U>().ObjectIsEmpty(Box<U>(*value));
        }

    protected:
        void Serialize(IntermediateWriter& output, const std::shared_ptr<U>& value,
                       const ContentSerializerAttribute& format) override
        {
            IntermediateSerializer::TypeSerializerFor<U>().InvokeSerialize(output, Box<U>(*value), format);
        }

        [[nodiscard]] std::shared_ptr<U> Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                     std::shared_ptr<U> existingInstance) override
        {
            std::shared_ptr<U> result = existingInstance ? existingInstance : std::make_shared<U>();
            *result = Unbox<U>(IntermediateSerializer::TypeSerializerFor<U>().InvokeDeserialize(input, format, Box<U>(*result)));
            return result;
        }

        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const std::shared_ptr<U>& value) override
        {
            if (value)
            {
                IntermediateSerializer::TypeSerializerFor<U>().InvokeScanChildren(serializer, callback, Box<U>(*value));
            }
        }
    };

    /**
     * @brief Serializes `ExternalReference<T>` as `<Reference>#External1</Reference>` with the
     *        filename recorded in the document's `ExternalReferences` section.
     *
     * @tparam T The referenced content type.
     */
    template<typename T>
    class CNAEXT ExternalReferenceSerializer final : public ContentTypeSerializer<ExternalReference<T>>
    {
    public:
        /** @brief Creates the serializer. */
        ExternalReferenceSerializer() = default;

        /** @brief Filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

    protected:
        void Serialize(IntermediateWriter& output, const std::shared_ptr<ExternalReference<T>>& value,
                       const ContentSerializerAttribute& format) override
        {
            (void)format;
            output.WriteExternalReference<T>(*value);
        }

        [[nodiscard]] std::shared_ptr<ExternalReference<T>> Deserialize(
            IntermediateReader& input, const ContentSerializerAttribute& format,
            std::shared_ptr<ExternalReference<T>> existingInstance) override
        {
            (void)format;
            if (!existingInstance)
            {
                existingInstance = std::make_shared<ExternalReference<T>>();
            }
            input.ReadExternalReference<T>(existingInstance);
            return existingInstance;
        }
    };

    namespace detail
    {
        /** @brief True when @p T has a content description: its own `DescribeContent` or a
         *  specialization of `ContentTypeDescription<T>`. */
        template<typename T>
        inline constexpr bool HasContentDescription =
            std::is_class_v<T> && (requires(ContentTypeDescriptor<T>& d) { T::DescribeContent(d); } ||
                                   !requires { ContentTypeDescription<T>::IsPrimary; });

        template<typename T>
        struct TypeSerializerFactory
        {
            static ContentTypeSerializerBase& Create()
            {
                if constexpr (std::is_enum_v<T>)
                {
                    return IntermediateSerializer::RegisterTypeSerializer(std::make_unique<EnumSerializer<T>>());
                }
                else if constexpr (HasContentDescription<T>)
                {
                    return IntermediateSerializer::RegisterTypeSerializer(std::make_unique<DescribedTypeSerializer<T>>());
                }
                else
                {
                    // A built-in or a user serializer registered with AddTypeSerializer: nothing to
                    // create, so an unregistered type is a runtime refusal rather than a compile error.
                    return IntermediateSerializer::RequireTypeSerializer(System::Type::From<T>(), ContentTypeName<T>::Name());
                }
            }
        };

        template<typename T>
        struct TypeSerializerFactory<std::vector<T>>
        {
            static ContentTypeSerializerBase& Create()
            {
                return IntermediateSerializer::RegisterTypeSerializer(
                    std::make_unique<ListSerializer<T>>(), {ContentTypeName<DeclaredContentType<T>>::Name() + "[]"});
            }
        };

        template<typename K, typename V>
        struct TypeSerializerFactory<std::map<K, V>>
        {
            static ContentTypeSerializerBase& Create()
            {
                return IntermediateSerializer::RegisterTypeSerializer(std::make_unique<DictionarySerializer<K, V>>());
            }
        };

        template<typename T>
        struct TypeSerializerFactory<std::optional<T>>
        {
            static ContentTypeSerializerBase& Create()
            {
                return IntermediateSerializer::RegisterTypeSerializer(std::make_unique<NullableSerializer<T>>(), {}, false);
            }
        };

        template<typename U>
        struct TypeSerializerFactory<std::shared_ptr<U>>
        {
            static ContentTypeSerializerBase& Create()
            {
                if constexpr (std::is_base_of_v<System::Object, U>)
                {
                    return IntermediateSerializer::TypeSerializerFor<U>();
                }
                else
                {
                    return IntermediateSerializer::RegisterTypeSerializer(std::make_unique<ReferenceSerializer<U>>(), {}, false);
                }
            }
        };

        template<typename T>
        struct TypeSerializerFactory<ExternalReference<T>>
        {
            static ContentTypeSerializerBase& Create()
            {
                IntermediateSerializer::RegisterKnownTypeName(ContentTypeName<T>::Name());
                return IntermediateSerializer::RegisterTypeSerializer(std::make_unique<ExternalReferenceSerializer<T>>());
            }
        };
    }
}

// The framework enumerations the built-in Curve serializer writes by name.
CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::CurveLoopType, "Microsoft.Xna.Framework.CurveLoopType", false,
                     {Microsoft::Xna::Framework::CurveLoopType::Constant, "Constant"},
                     {Microsoft::Xna::Framework::CurveLoopType::Cycle, "Cycle"},
                     {Microsoft::Xna::Framework::CurveLoopType::CycleOffset, "CycleOffset"},
                     {Microsoft::Xna::Framework::CurveLoopType::Oscillate, "Oscillate"},
                     {Microsoft::Xna::Framework::CurveLoopType::Linear, "Linear"});

CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::CurveContinuity, "Microsoft.Xna.Framework.CurveContinuity", false,
                     {Microsoft::Xna::Framework::CurveContinuity::Smooth, "Smooth"},
                     {Microsoft::Xna::Framework::CurveContinuity::Step, "Step"});
