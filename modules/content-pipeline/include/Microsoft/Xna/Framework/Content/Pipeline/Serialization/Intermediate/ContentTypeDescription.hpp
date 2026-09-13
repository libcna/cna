// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/detail/IntermediateSerializerCore.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Names the members of an enumeration for the intermediate format, where XNA writes an
     *        enum by its member name (`Happy`) and a `[Flags]` enum as `Cheese, Olives`
     *        (docs/xna-intermediate-xml-format.md §4).
     *
     * Specialize for every enum that appears in described content, or use
     * `CNA_XNA_CONTENT_ENUM_NAMES`. `Names` lists every member in ascending value order.
     *
     * @tparam E The enumeration.
     */
    template<typename E>
    struct CNAEXT ContentEnumNames
    {
        static_assert(sizeof(E) == 0,
                      "ContentEnumNames<E>: specialize this trait (or use CNA_XNA_CONTENT_ENUM_NAMES) for the enum.");
    };

/**
 * @brief Declares `ContentEnumNames` and `ContentTypeName` for one enumeration. Use at global
 *        scope.
 *
 * @param enumType The enumeration type.
 * @param dotNetName The .NET full name of the enumeration.
 * @param flags True for a `[Flags]` enumeration.
 * @param ... `{enumType::Member, "Member"}` pairs in ascending value order.
 */
#define CNA_XNA_CONTENT_ENUM(enumType, dotNetName, flags, ...)                                    \
    template<>                                                                                    \
    struct CNAEXT Microsoft::Xna::Framework::Content::Pipeline::ContentTypeName<enumType>          \
    {                                                                                             \
        [[nodiscard]] static std::string Name() { return dotNetName; }                            \
    };                                                                                            \
    template<>                                                                                    \
    struct CNAEXT Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentEnumNames<enumType> \
    {                                                                                             \
        static constexpr bool Flags = flags;                                                      \
        static constexpr std::array Names = std::to_array<std::pair<enumType, std::string_view>>({__VA_ARGS__}); \
    }

    template<typename T>
    class ContentTypeDescriptor;

    template<typename T>
    struct ContentTypeDescription;

    namespace detail
    {
        template<typename M>
        struct DeclaredContentTypeOf
        {
            using type = M;
        };

        /** @brief A `std::shared_ptr<U>` field of a `System::Object`-derived `U` declares `U`. */
        template<typename U>
        struct DeclaredContentTypeOf<std::shared_ptr<U>>
        {
            using type = std::conditional_t<std::is_base_of_v<System::Object, U>, U, std::shared_ptr<U>>;
        };

        /** @brief The content type a member of C++ type @p M declares. */
        template<typename M>
        using DeclaredContentType = typename DeclaredContentTypeOf<std::remove_cvref_t<M>>::type;

        /**
         * @brief One described member: its format, its declared serializer and type-erased
         *        accessors over the owning object.
         */
        template<typename T>
        struct MemberBinding
        {
            std::string name;
            ContentSerializerAttribute format;
            bool ignored = false;
            bool readOnly = false;
            std::function<ContentTypeSerializerBase&()> serializer;
            std::function<ContentObject(const T&)> get;
            std::function<void(T&, const ContentObject&)> set;

            /** @brief Adapts a base class's binding to the derived type. */
            template<typename Derived>
            [[nodiscard]] MemberBinding<Derived> ForDerived() const
            {
                MemberBinding<Derived> result;
                result.name = name;
                result.format = format;
                result.ignored = ignored;
                result.readOnly = readOnly;
                result.serializer = serializer;
                auto getter = get;
                auto setter = set;
                result.get = [getter](const Derived& owner) { return getter(static_cast<const T&>(owner)); };
                result.set = [setter](Derived& owner, const ContentObject& value) { setter(static_cast<T&>(owner), value); };
                return result;
            }
        };
    }

    /**
     * @brief Fluent settings of one described member: the `[ContentSerializer]` properties and
     *        `[ContentSerializerIgnore]`.
     *
     * @tparam T The described type.
     */
    template<typename T>
    class CNAEXT ContentMemberDescriptor
    {
    public:
        /**
         * @brief Wraps a binding.
         *
         * @param binding The binding being described.
         */
        explicit ContentMemberDescriptor(detail::MemberBinding<T>& binding) noexcept : binding_(&binding) {}

        /** @brief `[ContentSerializer(Optional = true)]`. */
        ContentMemberDescriptor& Optional(bool value = true)
        {
            binding_->format.setOptionalProperty(value);
            return *this;
        }

        /** @brief `[ContentSerializer(AllowNull = …)]`. */
        ContentMemberDescriptor& AllowNull(bool value)
        {
            binding_->format.setAllowNullProperty(value);
            return *this;
        }

        /** @brief `[ContentSerializer(ElementName = "…")]`. */
        ContentMemberDescriptor& ElementName(std::string value)
        {
            binding_->format.setElementNameProperty(std::move(value));
            return *this;
        }

        /** @brief `[ContentSerializer(FlattenContent = true)]`. */
        ContentMemberDescriptor& FlattenContent(bool value = true)
        {
            binding_->format.setFlattenContentProperty(value);
            return *this;
        }

        /** @brief `[ContentSerializer(SharedResource = true)]`. */
        ContentMemberDescriptor& SharedResource(bool value = true)
        {
            binding_->format.setSharedResourceProperty(value);
            return *this;
        }

        /** @brief `[ContentSerializer(CollectionItemName = "…")]`. */
        ContentMemberDescriptor& CollectionItemName(std::string value)
        {
            binding_->format.setCollectionItemNameProperty(std::move(value));
            return *this;
        }

        /** @brief Replaces every setting with a prepared attribute. */
        ContentMemberDescriptor& Format(const ContentSerializerAttribute& value)
        {
            const std::string name = binding_->format.getElementNameProperty();
            binding_->format = value;
            if (binding_->format.getElementNameProperty().empty())
            {
                binding_->format.setElementNameProperty(name);
            }
            return *this;
        }

        /** @brief `[ContentSerializerIgnore]`: the member is neither written nor accepted. */
        ContentMemberDescriptor& Ignore()
        {
            binding_->ignored = true;
            return *this;
        }

    private:
        detail::MemberBinding<T>* binding_;
    };

    /**
     * @brief Describes a content type for the intermediate serializer and the automatic type
     *        writer: its serializable members in XNA's order (public properties, then public
     *        fields, each in declaration order) and its type-level attributes.
     *
     * C++ has no reflection, so a type either declares
     * `static void DescribeContent(ContentTypeDescriptor<T>& d)` or specializes
     * `ContentTypeDescription<T>` (docs/xna-content-pipeline-compat-api.md §8).
     *
     * @tparam T The described type.
     */
    template<typename T>
    class CNAEXT ContentTypeDescriptor
    {
    public:
        /** @brief Creates an empty description. */
        ContentTypeDescriptor() = default;

        /**
         * @brief Describes a public field.
         *
         * @tparam M The field type.
         * @param name The member name (the default element name).
         * @param field Pointer to the field.
         * @return The member's fluent settings.
         */
        template<typename M>
        ContentMemberDescriptor<T> Field(std::string name, M T::*field)
        {
            using D = detail::DeclaredContentType<M>;
            detail::MemberBinding<T> binding;
            binding.name = name;
            binding.format.setElementNameProperty(std::move(name));
            binding.serializer = []() -> ContentTypeSerializerBase& { return IntermediateSerializer::TypeSerializerFor<D>(); };
            binding.get = [field](const T& owner) { return ToContentObject<D>(owner.*field); };
            binding.set = [field](T& owner, const ContentObject& value) { owner.*field = FromContentObject<D>(value); };
            members_.push_back(std::move(binding));
            return ContentMemberDescriptor<T>(members_.back());
        }

        /**
         * @brief Describes a public property with a getter and a setter.
         *
         * @tparam G Callable `M (const T&)`.
         * @tparam S Callable `void (T&, M)`.
         * @param name The member name.
         * @param getter Reads the value.
         * @param setter Writes the value.
         * @return The member's fluent settings.
         */
        template<typename G, typename S>
        ContentMemberDescriptor<T> Property(std::string name, G getter, S setter)
        {
            using M = std::remove_cvref_t<std::invoke_result_t<G, const T&>>;
            using D = detail::DeclaredContentType<M>;
            detail::MemberBinding<T> binding;
            binding.name = name;
            binding.format.setElementNameProperty(std::move(name));
            binding.serializer = []() -> ContentTypeSerializerBase& { return IntermediateSerializer::TypeSerializerFor<D>(); };
            binding.get = [getter](const T& owner) { return ToContentObject<D>(std::invoke(getter, owner)); };
            binding.set = [setter](T& owner, const ContentObject& value) { std::invoke(setter, owner, FromContentObject<D>(value)); };
            members_.push_back(std::move(binding));
            return ContentMemberDescriptor<T>(members_.back());
        }

        /**
         * @brief Describes a get-only collection property, which XNA fills in place: its items are
         *        appended to the existing collection on reading.
         *
         * @tparam G Callable `M& (T&)` returning the mutable collection.
         * @param name The member name.
         * @param getter Returns a reference to the collection.
         * @return The member's fluent settings.
         */
        template<typename G>
        ContentMemberDescriptor<T> ReadOnlyProperty(std::string name, G getter)
        {
            using M = std::remove_cvref_t<std::invoke_result_t<G, T&>>;
            using D = detail::DeclaredContentType<M>;
            detail::MemberBinding<T> binding;
            binding.name = name;
            binding.readOnly = true;
            binding.format.setElementNameProperty(std::move(name));
            binding.serializer = []() -> ContentTypeSerializerBase& { return IntermediateSerializer::TypeSerializerFor<D>(); };
            binding.get = [getter](const T& owner) { return ToContentObject<D>(std::invoke(getter, const_cast<T&>(owner))); };
            binding.set = [getter](T& owner, const ContentObject& value) { std::invoke(getter, owner) = FromContentObject<D>(value); };
            members_.push_back(std::move(binding));
            return ContentMemberDescriptor<T>(members_.back());
        }

        /**
         * @brief Includes the members of a base class first, as XNA serializes inherited members
         *        before the derived class's own.
         *
         * @tparam Base The base class; itself described.
         */
        template<typename Base>
        void BaseType()
        {
            static_assert(std::is_base_of_v<Base, T>, "BaseType<Base>(): Base must be a base class of T.");
            ContentTypeDescriptor<Base> base;
            ContentTypeDescription<Base>::Describe(base);
            std::vector<detail::MemberBinding<T>> inherited;
            for (const detail::MemberBinding<Base>& binding : base.Members())
            {
                inherited.push_back(binding.template ForDerived<T>());
            }
            inherited.insert(inherited.end(), members_.begin(), members_.end());
            members_ = std::move(inherited);
            if (collectionItemName_.empty())
            {
                collectionItemName_ = base.CollectionItemName();
            }
        }

        /** @brief `[ContentSerializerRuntimeType("…")]`. */
        void RuntimeType(std::string value) { runtimeType_ = std::move(value); }

        /** @brief `[ContentSerializerTypeVersion(n)]`. */
        void TypeVersion(std::int32_t value) noexcept { typeVersion_ = value; }

        /** @brief `[ContentSerializerCollectionItemName("…")]` on a collection type. */
        void CollectionItemName(std::string value) { collectionItemName_ = std::move(value); }

        /** @brief The described members in serialization order. */
        [[nodiscard]] const std::vector<detail::MemberBinding<T>>& Members() const noexcept { return members_; }

        /** @brief The declared run-time type, or empty. */
        [[nodiscard]] const std::string& RuntimeType() const noexcept { return runtimeType_; }

        /** @brief The declared type version. */
        [[nodiscard]] std::int32_t TypeVersion() const noexcept { return typeVersion_; }

        /** @brief The declared collection item name, or empty. */
        [[nodiscard]] const std::string& CollectionItemName() const noexcept { return collectionItemName_; }

    private:
        std::vector<detail::MemberBinding<T>> members_;
        std::string runtimeType_;
        std::int32_t typeVersion_ = 0;
        std::string collectionItemName_;
    };

    /**
     * @brief Supplies the content description of a type: by default the type's own
     *        `static void DescribeContent(ContentTypeDescriptor<T>&)`. Specialize for a type
     *        that cannot carry the member.
     *
     * @tparam T The described type.
     */
    template<typename T>
    struct CNAEXT ContentTypeDescription
    {
        /** @brief Marks the primary template; a specialization does not carry it. */
        static constexpr bool IsPrimary = true;

        /**
         * @brief Fills the descriptor.
         *
         * @param descriptor The descriptor to fill.
         */
        static void Describe(ContentTypeDescriptor<T>& descriptor)
        {
            static_assert(requires { T::DescribeContent(descriptor); },
                          "ContentTypeDescription<T>: declare `static void DescribeContent(ContentTypeDescriptor<T>&)` "
                          "on the type or specialize ContentTypeDescription<T>.");
            T::DescribeContent(descriptor);
        }
    };

    /**
     * @brief Throws the refusal for a shared-resource member on a type without identity.
     *
     * @param typeName The described type.
     * @param memberName The offending member.
     */
    CNAEXT [[noreturn]] void ThrowSharedResourceRequiresReference(const std::string& typeName,
                                                                  const std::string& memberName);

    /**
     * @brief The `object` serializer: the declared type of an `object` member. It never writes by
     *        itself -- the writer dispatches on the value's own type -- and reading requires a
     *        `Type` attribute.
     */
    class ObjectSerializer final : public ContentTypeSerializerBase
    {
    public:
        /** @brief Creates the serializer. */
        ObjectSerializer();

        /** @brief `object` values are nullable. */
        [[nodiscard]] bool IsNullable() const noexcept override;

        /** @brief `object` values are references. */
        [[nodiscard]] bool IsReferenceType() const noexcept override;

        /** @brief An empty box is the null object. */
        [[nodiscard]] bool IsNull(const ContentObject& value) const override;

        /** @brief The empty box. */
        [[nodiscard]] ContentObject NullObject() const override;

        /** @brief `object` cannot be instantiated for reading; a `Type` attribute is required. */
        [[nodiscard]] bool IsAbstract() const noexcept override;

    protected:
        void Serialize(IntermediateWriter& output, const ContentObject& value,
                       const ContentSerializerAttribute& format) override;
        [[nodiscard]] ContentObject Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                const ContentObject& existingInstance) override;
    };

    /**
     * @brief Serializes a described type member by member -- CNA's counterpart of the reflective
     *        serializer XNA gives every type that has no `ContentTypeSerializer` of its own.
     *
     * @tparam T The described type.
     */
    template<typename T>
    class CNAEXT DescribedTypeSerializer final : public ContentTypeSerializer<T>
    {
    public:
        using typename ContentTypeSerializer<T>::TargetCarrier;
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;

        /** @brief Builds the serializer from the type's description. */
        DescribedTypeSerializer()
        {
            ContentTypeDescription<T>::Describe(descriptor_);
            for (const detail::MemberBinding<T>& member : descriptor_.Members())
            {
                if (member.format.getSharedResourceProperty() && !detail::IsReferenceCarrier<T>)
                {
                    ThrowSharedResourceRequiresReference(ContentTypeName<T>::Name(), member.name);
                }
            }
        }

        /** @brief Reference types can be filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override
        {
            return detail::IsReferenceCarrier<T>;
        }

        /** @brief The declared collection item name of the type, or empty. */
        [[nodiscard]] std::string CollectionItemName() const override { return descriptor_.CollectionItemName(); }

        /** @brief The description this serializer follows. */
        [[nodiscard]] const ContentTypeDescriptor<T>& Description() const noexcept { return descriptor_; }

    protected:
        void Serialize(IntermediateWriter& output, const TargetCarrier& value,
                       const ContentSerializerAttribute& format) override
        {
            (void)format;
            const T& owner = Dereference(value);
            for (const detail::MemberBinding<T>& member : descriptor_.Members())
            {
                if (member.ignored)
                {
                    continue;
                }
                ContentObject child = member.get(owner);
                if (member.format.getSharedResourceProperty())
                {
                    output.WriteSharedResourceCore(child, member.format, member.serializer());
                }
                else
                {
                    output.WriteObjectCore(child, member.format, member.serializer(), false);
                }
            }
        }

        [[nodiscard]] TargetCarrier Deserialize(IntermediateReader& input, const ContentSerializerAttribute& format,
                                                TargetCarrier existingInstance) override
        {
            (void)format;
            TargetCarrier result = std::move(existingInstance);
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                if (result == nullptr)
                {
                    result = Unbox<T>(this->CreateInstance());
                }
            }
            T& owner = Dereference(result);
            for (const detail::MemberBinding<T>& member : descriptor_.Members())
            {
                if (member.ignored)
                {
                    continue;
                }
                if (member.format.getSharedResourceProperty())
                {
                    if constexpr (detail::IsReferenceCarrier<T>)
                    {
                        TargetCarrier keepAlive = result;
                        auto setter = member.set;
                        input.ReadSharedResourceCore(member.format, member.serializer(),
                                                     [keepAlive, setter](const ContentObject& value) { setter(*keepAlive, value); });
                    }
                    continue;
                }
                if (member.format.getOptionalProperty() && !member.format.getFlattenContentProperty() &&
                    !input.MoveToElement(member.format.getElementNameProperty()))
                {
                    continue;
                }
                ContentObject existing;
                if (member.readOnly && member.serializer().getCanDeserializeIntoExistingObjectProperty())
                {
                    existing = member.get(owner);
                }
                member.set(owner, input.ReadObjectCore(member.format, member.serializer(), existing));
            }
            return result;
        }

        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const TargetCarrier& value) override
        {
            const T& owner = Dereference(value);
            for (const detail::MemberBinding<T>& member : descriptor_.Members())
            {
                if (member.ignored)
                {
                    continue;
                }
                // A shared resource is always written with its `Type`, so the scan sees it as an
                // `object` child: that is what makes its namespace part of the root's declarations.
                // The member's own serializer is resolved first so that it is registered before the
                // value is looked up by its carrier type.
                ContentTypeSerializerBase& own = member.serializer();
                ContentTypeSerializerBase& declared = member.format.getSharedResourceProperty()
                                                          ? serializer.GetTypeSerializer(System::Type::From<System::Object>())
                                                          : own;
                callback(declared, member.get(owner));
            }
        }

    private:
        static const T& Dereference(const TargetCarrier& value)
        {
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                return *value;
            }
            else
            {
                return value;
            }
        }

        static T& Dereference(TargetCarrier& value)
        {
            if constexpr (detail::IsReferenceCarrier<T>)
            {
                return *value;
            }
            else
            {
                return value;
            }
        }

        ContentTypeDescriptor<T> descriptor_;
    };
}
