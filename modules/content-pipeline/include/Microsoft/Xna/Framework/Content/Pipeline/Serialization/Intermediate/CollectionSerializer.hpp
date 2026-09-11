// SPDX-License-Identifier: MS-PL
#pragma once

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/BuiltInTypeSerializers.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Serializes a named collection type -- one deriving `System::Collections::ObjectModel::
     *        Collection<TElement>` -- exactly as the list serializer handles a `std::vector`:
     *        packed into text when the element is a single-token type, one element apiece otherwise.
     *
     * XNA reaches these through the same reflective path it uses for `List<T>`, which is why an
     * `IndexCollection` writes `0 1 2` and a `BoneWeightCollection` writes one empty `<Item />` per
     * weight (measured, tests/reference/xna40/graphics cases indexcollection/serialize and
     * boneweight/serialize).
     *
     * @tparam TCollection The collection type, a class deriving Collection<TElement>.
     * @tparam TElement The element type.
     */
    template<typename TCollection, typename TElement>
    class CNAEXT CollectionSerializer final : public ContentTypeSerializer<TCollection>
    {
        using Element = detail::DeclaredContentType<TElement>;
        using ChildCallback = ContentTypeSerializerBase::ChildCallback;
        using Carrier = Microsoft::Xna::Framework::Content::Pipeline::Carrier<TCollection>;

    public:
        /** @brief Creates the serializer. */
        CollectionSerializer() = default;

        /** @brief Collections are filled in place. */
        [[nodiscard]] bool getCanDeserializeIntoExistingObjectProperty() const override { return true; }

        /** @brief An empty collection has nothing to write. */
        [[nodiscard]] bool ObjectIsEmpty(const Carrier& value) const override
        {
            return Items(value).empty();
        }

    protected:
        /**
         * @brief Writes every element of the collection.
         *
         * @param output The intermediate writer.
         * @param value The collection.
         * @param format The content format of the collection's own element.
         */
        void Serialize(IntermediateWriter& output, const Carrier& value,
                       const Content::ContentSerializerAttribute& format) override
        {
            ContentTypeSerializerBase& element = IntermediateSerializer::TypeSerializerFor<Element>();
            const std::vector<TElement> items = Items(value);
            if (element.PackedTokenCount() > 0)
            {
                std::string text;
                for (const TElement& item : items)
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
            Content::ContentSerializerAttribute itemFormat;
            itemFormat.setElementNameProperty(format.getCollectionItemNameProperty());
            for (const TElement& item : items)
            {
                output.WriteObjectCore(ToContentObject<Element>(item), itemFormat, element, false);
            }
        }

        /**
         * @brief Reads every element into the collection.
         *
         * @param input The intermediate reader.
         * @param format The content format of the collection's own element.
         * @param existingInstance The collection receiving the elements.
         * @return The filled collection.
         */
        [[nodiscard]] Carrier Deserialize(IntermediateReader& input,
                                          const Content::ContentSerializerAttribute& format,
                                          Carrier existingInstance) override
        {
            Carrier result = Prepare(std::move(existingInstance));
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
                    Target(result).Add(FromContentObject<Element>(
                        element.ParsePacked(std::span<const std::string>(tokens.data() + i, width))));
                }
                return result;
            }
            Content::ContentSerializerAttribute itemFormat;
            itemFormat.setElementNameProperty(format.getCollectionItemNameProperty());
            while (input.MoveToElement(itemFormat.getElementNameProperty()))
            {
                Target(result).Add(
                    FromContentObject<Element>(input.ReadObjectCore(itemFormat, element, ContentObject{})));
            }
            return result;
        }

        /**
         * @brief Announces every element, so the namespace scan sees it.
         *
         * @param serializer The content serializer.
         * @param callback Invoked for each element.
         * @param value The collection.
         */
        void ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                          const Carrier& value) override
        {
            (void)serializer;
            ContentTypeSerializerBase& element = IntermediateSerializer::TypeSerializerFor<Element>();
            for (const TElement& item : Items(value))
            {
                callback(element, ToContentObject<Element>(item));
            }
        }

    private:
        /** @brief The collection behind the carrier, whichever the carrier is. */
        static TCollection& Target(Carrier& value)
        {
            if constexpr (detail::IsSharedPtr<Carrier>::value)
            {
                return *value;
            }
            else
            {
                return value;
            }
        }

        /** @brief A copy of the elements, since Collection<T> is indexed rather than iterated. */
        static std::vector<TElement> Items(const Carrier& value)
        {
            std::vector<TElement> items;
            if constexpr (detail::IsSharedPtr<Carrier>::value)
            {
                if (value == nullptr)
                {
                    return items;
                }
            }
            const TCollection& collection = [&value]() -> const TCollection&
            {
                if constexpr (detail::IsSharedPtr<Carrier>::value)
                {
                    return *value;
                }
                else
                {
                    return value;
                }
            }();
            const auto& indexed =
                static_cast<const System::Collections::ObjectModel::Collection<TElement>&>(collection);
            items.reserve(static_cast<std::size_t>(indexed.getCountProperty()));
            for (SharpRuntime::intcs i = 0; i < indexed.getCountProperty(); ++i)
            {
                items.push_back(indexed[i]);
            }
            return items;
        }

        /** @brief The collection to fill: the one handed in, or a new one. */
        static Carrier Prepare(Carrier existingInstance)
        {
            if constexpr (detail::IsSharedPtr<Carrier>::value)
            {
                if (existingInstance == nullptr)
                {
                    return std::make_shared<TCollection>();
                }
            }
            return existingInstance;
        }
    };
}
