// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <span>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/PixelTraits.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Object.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    class VertexBufferContent;
}

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    class VertexContent;

    /**
     * @brief Provides methods and properties for maintaining a vertex channel: a list of arbitrary
     *        data, one entry per vertex, under an encoded channel name.
     *
     * XNA calls this type `VertexChannel` and its typed form `VertexChannel<T>`. A class and a
     * class template cannot share a name in C++, so the base is `VertexChannelBase` here, as
     * `ContentTypeSerializerBase` is for the same reason.
     */
    class VertexChannelBase : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel";

        /**
         * @brief Gets the number of entries in the channel, one per vertex.
         *
         * @return The entry count.
         */
        [[nodiscard]] virtual SharpRuntime::intcs getCountProperty() const noexcept = 0;

        /**
         * @brief Gets the type of each entry.
         *
         * @return The element type.
         */
        [[nodiscard]] virtual System::Type getElementTypeProperty() const = 0;

        /**
         * @brief Gets the .NET name of the element type, which the refusals quote.
         *
         * @return The element type's full name.
         */
        CNAEXT [[nodiscard]] virtual std::string ElementTypeName() const = 0;

        /**
         * @brief Gets the name of the channel, encoded as a base name and a usage index.
         *
         * @return The channel name.
         */
        [[nodiscard]] const std::string& getNameProperty() const noexcept;

        /**
         * @brief Gets one entry of the channel, boxed as XNA's `object` indexer answers it.
         *
         * @param index The entry index.
         * @return The boxed entry.
         * @throws System::ArgumentOutOfRangeException when the index is outside the channel.
         */
        [[nodiscard]] virtual ContentObject operator[](SharpRuntime::intcs index) const = 0;

        /**
         * @brief Determines whether the channel contains the given entry.
         *
         * @param value The boxed entry to look for.
         * @return true when an equal entry is in the channel.
         */
        [[nodiscard]] virtual bool Contains(const ContentObject& value) const = 0;

        /**
         * @brief Copies the entries into a list, starting at the given index.
         *
         * @param destination The list receiving the entries, boxed.
         * @param index The first index to write at.
         * @throws System::ArgumentOutOfRangeException when the index is negative.
         * @throws System::ArgumentException when the entries do not fit.
         */
        void CopyTo(std::vector<ContentObject>& destination, SharpRuntime::intcs index) const;

        /**
         * @brief Gets the index of the given entry.
         *
         * @param value The boxed entry to look for.
         * @return The index, or -1 when the channel holds no equal entry.
         */
        [[nodiscard]] virtual SharpRuntime::intcs IndexOf(const ContentObject& value) const = 0;

        /**
         * @brief Reads the channel converted to another element type.
         *
         * The conversion runs through `Vector4`, which is how the pipeline's own `VectorConverter`
         * converts between vertex element types (measured, tests/reference/xna40/graphics case
         * vertexcontent/channel_read_converted).
         *
         * @tparam TargetType The wanted element type.
         * @return The converted entries.
         * @throws System::InvalidOperationException when either type has no vector conversion.
         */
        template<typename TargetType>
        [[nodiscard]] std::vector<TargetType> ReadConvertedContent() const
        {
            static_assert(detail::ValidPixelType<TargetType>,
                          "VertexChannelBase::ReadConvertedContent<T>: T must be a vertex element type.");
            std::vector<TargetType> converted;
            converted.reserve(static_cast<std::size_t>(getCountProperty()));
            for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
            {
                converted.push_back(detail::PixelTraits<TargetType>::FromVector4(ElementAsVector4(i)));
            }
            return converted;
        }

        /**
         * @brief Gets one entry as a `Vector4`, the pipeline's conversion currency.
         *
         * @param index The entry index.
         * @return The entry converted to a vector.
         * @throws System::InvalidOperationException when the element type has no conversion.
         */
        CNAEXT [[nodiscard]] virtual Vector4 ElementAsVector4(SharpRuntime::intcs index) const = 0;

        /**
         * @brief Gets the entries as intermediate-format text, one token per component.
         *
         * @return The packed entries.
         */
        CNAEXT [[nodiscard]] virtual std::string PackedContent() const = 0;

        /**
         * @brief Replaces the entries from intermediate-format text.
         *
         * @param tokens The packed entries.
         */
        CNAEXT virtual void SetPackedContent(const std::vector<std::string>& tokens) = 0;

        /**
         * @brief Inserts a default entry, keeping the channel as long as its vertex content.
         *
         * Adding a vertex gives every channel a default entry at that position, and removing one
         * takes it away again (measured, tests/reference/xna40/graphics cases
         * vertexcontent/add_with_channel, insert_with_channel and remove_with_channel).
         *
         * @param index The entry index to insert at.
         */
        CNAEXT virtual void InsertDefaultEntry(SharpRuntime::intcs index) = 0;

        /**
         * @brief Removes one entry, keeping the channel as long as its vertex content.
         *
         * @param index The entry index to remove.
         */
        CNAEXT virtual void RemoveEntry(SharpRuntime::intcs index) = 0;

        /**
         * @brief Writes every entry into a vertex buffer, one per vertex.
         *
         * @param buffer The buffer to write into.
         * @param offset The byte offset of this channel within a vertex.
         * @param stride The number of bytes one vertex occupies.
         */
        CNAEXT virtual void WriteInto(Processors::VertexBufferContent& buffer, SharpRuntime::intcs offset,
                                      SharpRuntime::intcs stride) const = 0;

        /**
         * @brief Appends one boxed entry, for the type-erased channel routes.
         *
         * @param value The entry; it must hold this channel's element type.
         * @throws System::ArgumentException when the entry is of another type.
         */
        CNAEXT virtual void AddEntry(const ContentObject& value) = 0;

        /**
         * @brief Rebuilds the entries in the given order, which is how a mesh is reordered without
         *        knowing what its channels hold.
         *
         * @param order The old index of each new entry; it must name every entry once.
         * @throws System::ArgumentException when the order is not a permutation of the entries.
         */
        CNAEXT virtual void ReorderEntries(const std::vector<SharpRuntime::intcs>& order) = 0;

        /**
         * @brief Determines whether two entries of this channel are equal.
         *
         * @param first The first entry index.
         * @param second The second entry index.
         * @return true when the entries compare equal.
         * @throws System::ArgumentOutOfRangeException when an index is outside the channel.
         */
        CNAEXT [[nodiscard]] virtual bool EntriesEqual(SharpRuntime::intcs first,
                                                       SharpRuntime::intcs second) const = 0;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this channel's type, as XNA's `ToString` does.
         *
         * @return The .NET full name, with the element type for a typed channel.
         */
        [[nodiscard]] virtual std::string ToString() const;

    protected:
        /**
         * @brief Initializes a channel with a name.
         *
         * @param name The encoded channel name.
         */
        explicit VertexChannelBase(std::string name);

        /** @brief The refusal .NET gives for an index outside a list. */
        [[noreturn]] static void ThrowIndexOutOfRange();

        /** @brief The refusal for an element type the pipeline cannot convert through Vector4. */
        [[noreturn]] static void ThrowNoVectorConversion(System::Type elementType);

    private:
        std::string name_;
    };

    /**
     * @brief Provides methods and properties for maintaining a vertex channel of one element type.
     *
     * @tparam T The element type.
     */
    template<typename T>
    class VertexChannel final : public VertexChannelBase
    {
    public:
        /**
         * @brief Initializes a channel with a name and no entries.
         *
         * @param name The encoded channel name.
         */
        explicit VertexChannel(std::string name) : VertexChannelBase(std::move(name)) {}

        /**
         * @brief Initializes a channel with a name and entries.
         *
         * @param name The encoded channel name.
         * @param items The entries.
         */
        VertexChannel(std::string name, std::vector<T> items)
            : VertexChannelBase(std::move(name)), items_(std::move(items))
        {
        }

        /** @brief The number of entries. */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const noexcept override
        {
            return static_cast<SharpRuntime::intcs>(items_.size());
        }

        /** @brief The element type of this channel. */
        [[nodiscard]] System::Type getElementTypeProperty() const override { return System::Type::From<T>(); }

        /** @brief The .NET name of the element type. */
        CNAEXT [[nodiscard]] std::string ElementTypeName() const override { return ContentTypeName<T>::Name(); }

        /**
         * @brief Gets one entry, boxed.
         *
         * @param index The entry index.
         * @return The boxed entry.
         * @throws System::ArgumentOutOfRangeException when the index is outside the channel.
         */
        [[nodiscard]] ContentObject operator[](SharpRuntime::intcs index) const override
        {
            return Box<T>(At(index));
        }

        /**
         * @brief Gets one entry.
         *
         * @param index The entry index.
         * @return The entry.
         * @throws System::ArgumentOutOfRangeException when the index is outside the channel.
         */
        [[nodiscard]] const T& At(SharpRuntime::intcs index) const
        {
            if (index < 0 || static_cast<std::size_t>(index) >= items_.size())
            {
                ThrowIndexOutOfRange();
            }
            return items_[static_cast<std::size_t>(index)];
        }

        /**
         * @brief Replaces one entry.
         *
         * @param index The entry index.
         * @param value The new entry.
         * @throws System::ArgumentOutOfRangeException when the index is outside the channel.
         */
        void SetAt(SharpRuntime::intcs index, const T& value)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= items_.size())
            {
                ThrowIndexOutOfRange();
            }
            items_[static_cast<std::size_t>(index)] = value;
        }

        /**
         * @brief Determines whether the channel contains the given entry.
         *
         * @param value The entry to look for.
         * @return true when an equal entry is in the channel.
         */
        [[nodiscard]] bool Contains(const T& value) const { return IndexOf(value) >= 0; }

        /** @brief Determines whether the channel contains the given boxed entry. */
        [[nodiscard]] bool Contains(const ContentObject& value) const override
        {
            return Holds<T>(value) && Contains(Unbox<T>(value));
        }

        /**
         * @brief Gets the index of the given entry.
         *
         * @param value The entry to look for.
         * @return The index, or -1 when the channel holds no equal entry.
         */
        [[nodiscard]] SharpRuntime::intcs IndexOf(const T& value) const
        {
            for (std::size_t i = 0; i < items_.size(); ++i)
            {
                const bool equal = [&]
                {
                    if constexpr (detail::ValidPixelType<T>)
                    {
                        return detail::PixelTraits<T>::Equal(items_[i], value);
                    }
                    else
                    {
                        return items_[i] == value;
                    }
                }();
                if (equal)
                {
                    return static_cast<SharpRuntime::intcs>(i);
                }
            }
            return -1;
        }

        /** @brief Gets the index of the given boxed entry. */
        [[nodiscard]] SharpRuntime::intcs IndexOf(const ContentObject& value) const override
        {
            return Holds<T>(value) ? IndexOf(Unbox<T>(value)) : -1;
        }

        /**
         * @brief Copies the entries into a list, starting at the given index.
         *
         * @param destination The list receiving the entries.
         * @param index The first index to write at.
         * @throws System::ArgumentOutOfRangeException when the index is negative.
         * @throws System::ArgumentException when the entries do not fit.
         */
        void CopyTo(std::vector<T>& destination, SharpRuntime::intcs index) const
        {
            if (index < 0)
            {
                throw System::ArgumentOutOfRangeException("index");
            }
            if (destination.size() < static_cast<std::size_t>(index) + items_.size())
            {
                throw System::ArgumentException("The channel does not fit into the destination.", "destination");
            }
            std::copy(items_.begin(), items_.end(), destination.begin() + static_cast<std::ptrdiff_t>(index));
        }

        /** @brief The entries themselves. */
        CNAEXT [[nodiscard]] const std::vector<T>& Items() const noexcept { return items_; }

        /** @brief The entries themselves, mutable. */
        CNAEXT [[nodiscard]] std::vector<T>& Items() noexcept { return items_; }

        /** @brief One entry as a vector, or a refusal when the element type has no conversion. */
        CNAEXT [[nodiscard]] Vector4 ElementAsVector4(SharpRuntime::intcs index) const override
        {
            if constexpr (detail::ValidPixelType<T>)
            {
                return detail::PixelTraits<T>::ToVector4(At(index));
            }
            else
            {
                // A channel of indices has no vector form, which is what XNA's VectorConverter
                // says of any type outside its table.
                ThrowNoVectorConversion(getElementTypeProperty());
            }
        }

        /** @brief Writes every entry into a vertex buffer, one per vertex. */
        CNAEXT void WriteInto(Processors::VertexBufferContent& buffer, SharpRuntime::intcs offset,
                              SharpRuntime::intcs stride) const override;

        /** @brief Appends one boxed entry. */
        CNAEXT void ReorderEntries(const std::vector<SharpRuntime::intcs>& order) override
        {
            if (order.size() != items_.size())
            {
                throw System::ArgumentException("The order must name every entry of the channel once.",
                                                "order");
            }
            std::vector<T> reordered;
            reordered.reserve(items_.size());
            for (const SharpRuntime::intcs index : order)
            {
                if (index < 0 || static_cast<std::size_t>(index) >= items_.size())
                {
                    throw System::ArgumentException("The order must name every entry of the channel once.",
                                                    "order");
                }
                reordered.push_back(items_[static_cast<std::size_t>(index)]);
            }
            items_ = std::move(reordered);
        }

        CNAEXT [[nodiscard]] bool EntriesEqual(SharpRuntime::intcs first,
                                               SharpRuntime::intcs second) const override
        {
            if (first < 0 || second < 0 || static_cast<std::size_t>(first) >= items_.size() ||
                static_cast<std::size_t>(second) >= items_.size())
            {
                throw System::ArgumentOutOfRangeException("index");
            }
            const T& a = items_[static_cast<std::size_t>(first)];
            const T& b = items_[static_cast<std::size_t>(second)];
            if constexpr (detail::ValidPixelType<T>)
            {
                return detail::PixelTraits<T>::Equal(a, b);
            }
            else
            {
                return a == b;
            }
        }

        CNAEXT void AddEntry(const ContentObject& value) override
        {
            if (!Holds<T>(value))
            {
                throw System::ArgumentException("The value is not of this channel's element type.", "value");
            }
            items_.push_back(Unbox<T>(value));
        }

        /** @brief Inserts a default entry at the given index. */
        CNAEXT void InsertDefaultEntry(SharpRuntime::intcs index) override
        {
            if (index < 0 || static_cast<std::size_t>(index) > items_.size())
            {
                ThrowIndexOutOfRange();
            }
            items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), T{});
        }

        /** @brief Removes the entry at the given index. */
        CNAEXT void RemoveEntry(SharpRuntime::intcs index) override
        {
            if (index < 0 || static_cast<std::size_t>(index) >= items_.size())
            {
                ThrowIndexOutOfRange();
            }
            items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        }

        /** @brief The entries as intermediate-format text, through the type's own serializer. */
        CNAEXT [[nodiscard]] std::string PackedContent() const override
        {
            Serialization::Intermediate::ContentTypeSerializerBase& serializer =
                Serialization::Intermediate::IntermediateSerializer::TypeSerializerFor<T>();
            std::string text;
            for (const T& item : items_)
            {
                if (!text.empty())
                {
                    text += ' ';
                }
                text += serializer.FormatPacked(Box<T>(item));
            }
            return text;
        }

        /** @brief Replaces the entries from intermediate-format text. */
        CNAEXT void SetPackedContent(const std::vector<std::string>& tokens) override
        {
            Serialization::Intermediate::ContentTypeSerializerBase& serializer =
                Serialization::Intermediate::IntermediateSerializer::TypeSerializerFor<T>();
            const std::size_t width = serializer.PackedTokenCount();
            items_.clear();
            if (width == 0)
            {
                return;
            }
            for (std::size_t i = 0; i + width <= tokens.size(); i += width)
            {
                items_.push_back(Unbox<T>(serializer.ParsePacked(
                    std::span<const std::string>(tokens.data() + i, width))));
            }
        }

        /** @brief The .NET name of this channel's closed generic type. */
        [[nodiscard]] std::string ToString() const override
        {
            return "Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel`1[" +
                   ContentTypeName<T>::Name() + "]";
        }

    private:
        std::vector<T> items_;
    };

    /**
     * @brief Provides methods and properties for managing a list of vertex channels.
     */
    class VertexChannelCollection final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannelCollection";

        /**
         * @brief Initializes a collection belonging to a vertex content.
         *
         * @param owner The vertex content whose channels these are; null for a free-standing
         *        collection, which is what the intermediate serializer fills.
         */
        CNAEXT explicit VertexChannelCollection(VertexContent* owner = nullptr) noexcept;

        /**
         * @brief Gets the number of channels.
         *
         * @return The channel count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const noexcept;

        /**
         * @brief Gets the channel at the given index.
         *
         * @param index The channel index.
         * @return The channel.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        [[nodiscard]] const std::shared_ptr<VertexChannelBase>& operator[](SharpRuntime::intcs index) const;

        /**
         * @brief Gets the channel with the given name.
         *
         * @param name The channel name.
         * @return The channel.
         * @throws System::Collections::Generic::KeyNotFoundException when there is no such channel.
         */
        [[nodiscard]] const std::shared_ptr<VertexChannelBase>& operator[](const std::string& name) const;

        /**
         * @brief Adds a channel of the given element type.
         *
         * @tparam ElementType The element type.
         * @param name The channel name.
         * @param channelData The entries; empty for a channel with none.
         * @return The new channel.
         * @throws System::ArgumentException when the name is already taken or the entry count does
         *         not match the vertex count.
         */
        template<typename ElementType>
        std::shared_ptr<VertexChannel<ElementType>> Add(const std::string& name,
                                                        std::vector<ElementType> channelData)
        {
            return Insert<ElementType>(getCountProperty(), name, std::move(channelData));
        }

        /**
         * @brief Adds a channel whose element type is named at run time.
         *
         * @param name The channel name.
         * @param elementType The element type.
         * @param channelData The entries, boxed; empty for a channel with none.
         * @return The new channel.
         * @throws System::ArgumentException when the name is already taken or the entry count does
         *         not match the vertex count.
         */
        std::shared_ptr<VertexChannelBase> Add(const std::string& name, System::Type elementType,
                                               const std::vector<ContentObject>& channelData);

        /**
         * @brief Inserts a channel whose element type is named at run time.
         *
         * @param index The index to insert at.
         * @param name The channel name.
         * @param elementType The element type.
         * @param channelData The entries, boxed; empty for a channel with none.
         * @return The new channel.
         * @throws System::ArgumentException when the name is already taken or the entry count does
         *         not match the vertex count.
         */
        std::shared_ptr<VertexChannelBase> Insert(SharpRuntime::intcs index, const std::string& name,
                                                  System::Type elementType,
                                                  const std::vector<ContentObject>& channelData);

        /** @brief Removes every channel. */
        void Clear() noexcept;

        /**
         * @brief Determines whether the collection contains the given channel.
         *
         * @param channel The channel to look for.
         * @return true when it is in the collection.
         */
        [[nodiscard]] bool Contains(const std::shared_ptr<VertexChannelBase>& channel) const noexcept;

        /**
         * @brief Determines whether the collection contains a channel with the given name.
         *
         * @param name The channel name.
         * @return true when such a channel is in the collection.
         */
        [[nodiscard]] bool Contains(const std::string& name) const noexcept;

        /**
         * @brief Converts a channel to another element type, in place.
         *
         * @tparam TargetType The wanted element type.
         * @param index The channel index.
         * @return The converted channel, which has replaced the original.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        template<typename TargetType>
        std::shared_ptr<VertexChannel<TargetType>> ConvertChannelContent(SharpRuntime::intcs index)
        {
            const std::shared_ptr<VertexChannelBase>& channel = (*this)[index];
            auto converted = std::make_shared<VertexChannel<TargetType>>(channel->getNameProperty(),
                                                                        channel->ReadConvertedContent<TargetType>());
            Replace(index, converted);
            return converted;
        }

        /**
         * @brief Converts a named channel to another element type, in place.
         *
         * @tparam TargetType The wanted element type.
         * @param name The channel name.
         * @return The converted channel, which has replaced the original.
         * @throws System::Collections::Generic::KeyNotFoundException when there is no such channel.
         */
        template<typename TargetType>
        std::shared_ptr<VertexChannel<TargetType>> ConvertChannelContent(const std::string& name)
        {
            return ConvertChannelContent<TargetType>(RequireIndexOf(name));
        }

        /**
         * @brief Gets a typed channel by index.
         *
         * @tparam T The expected element type.
         * @param index The channel index.
         * @return The channel.
         * @throws System::InvalidOperationException when the channel has another element type.
         */
        template<typename T>
        [[nodiscard]] std::shared_ptr<VertexChannel<T>> Get(SharpRuntime::intcs index) const
        {
            const std::shared_ptr<VertexChannelBase>& channel = (*this)[index];
            std::shared_ptr<VertexChannel<T>> typed = std::dynamic_pointer_cast<VertexChannel<T>>(channel);
            if (typed == nullptr)
            {
                ThrowWrongType(channel->getNameProperty(), channel->ElementTypeName(), ContentTypeName<T>::Name());
            }
            return typed;
        }

        /**
         * @brief Gets a typed channel by name.
         *
         * @tparam T The expected element type.
         * @param name The channel name.
         * @return The channel.
         * @throws System::Collections::Generic::KeyNotFoundException when there is no such channel.
         * @throws System::InvalidOperationException when the channel has another element type.
         */
        template<typename T>
        [[nodiscard]] std::shared_ptr<VertexChannel<T>> Get(const std::string& name) const
        {
            return Get<T>(RequireIndexOf(name));
        }

        /**
         * @brief Gets the index of the given channel.
         *
         * @param channel The channel to look for.
         * @return The index, or -1 when it is not in the collection.
         */
        [[nodiscard]] SharpRuntime::intcs IndexOf(const std::shared_ptr<VertexChannelBase>& channel) const noexcept;

        /**
         * @brief Gets the index of the channel with the given name.
         *
         * @param name The channel name.
         * @return The index, or -1 when there is no such channel.
         */
        [[nodiscard]] SharpRuntime::intcs IndexOf(const std::string& name) const noexcept;

        /**
         * @brief Inserts a channel of the given element type at the given index.
         *
         * @tparam ElementType The element type.
         * @param index The index to insert at.
         * @param name The channel name.
         * @param channelData The entries; empty for a channel with none.
         * @return The new channel.
         * @throws System::ArgumentException when the name is already taken or the entry count does
         *         not match the vertex count.
         */
        template<typename ElementType>
        std::shared_ptr<VertexChannel<ElementType>> Insert(SharpRuntime::intcs index, const std::string& name,
                                                           std::vector<ElementType> channelData)
        {
            RequireFreeName(name);
            RequireChannelSize(name, static_cast<SharpRuntime::intcs>(channelData.size()));
            auto channel = std::make_shared<VertexChannel<ElementType>>(name, std::move(channelData));
            InsertChannel(index, channel);
            return channel;
        }

        /**
         * @brief Removes the given channel.
         *
         * @param channel The channel to remove.
         * @return true when it was there.
         */
        bool Remove(const std::shared_ptr<VertexChannelBase>& channel);

        /**
         * @brief Removes the channel with the given name.
         *
         * @param name The channel name.
         * @return true when such a channel was there.
         */
        bool Remove(const std::string& name);

        /**
         * @brief Removes the channel at the given index.
         *
         * @param index The channel index.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        void RemoveAt(SharpRuntime::intcs index);

        /**
         * @brief Returns an iterator to the first channel, the C++ form of `GetEnumerator`.
         *
         * @return An iterator to the first channel.
         */
        [[nodiscard]] std::vector<std::shared_ptr<VertexChannelBase>>::const_iterator begin() const noexcept;

        /**
         * @brief Returns an iterator past the last channel.
         *
         * @return An iterator past the last channel.
         */
        [[nodiscard]] std::vector<std::shared_ptr<VertexChannelBase>>::const_iterator end() const noexcept;

        /**
         * @brief Inserts an already-built channel, as the intermediate serializer does.
         *
         * @param index The index to insert at.
         * @param channel The channel.
         * @throws System::ArgumentException when the name is already taken.
         */
        CNAEXT void InsertChannel(SharpRuntime::intcs index, const std::shared_ptr<VertexChannelBase>& channel);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        /** @brief The index of a named channel, or the refusal XNA gives for an unknown name. */
        [[nodiscard]] SharpRuntime::intcs RequireIndexOf(const std::string& name) const;

        /** @brief Refuses a name another channel already has. */
        void RequireFreeName(const std::string& name) const;

        /** @brief Refuses an entry count that is not the owner's vertex count. */
        void RequireChannelSize(const std::string& name, SharpRuntime::intcs size) const;

        /** @brief Replaces one channel, keeping its place. */
        void Replace(SharpRuntime::intcs index, const std::shared_ptr<VertexChannelBase>& channel);

        /** @brief The refusal for a channel asked for as the wrong type. */
        [[noreturn]] static void ThrowWrongType(const std::string& name, const std::string& actual,
                                                const std::string& expected);

        VertexContent* owner_;
        std::vector<std::shared_ptr<VertexChannelBase>> channels_;
    };
}

#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/VertexBufferContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    template<typename T>
    void VertexChannel<T>::WriteInto(Processors::VertexBufferContent& buffer, SharpRuntime::intcs offset,
                                     SharpRuntime::intcs stride) const
    {
        if constexpr (detail::ValidPixelType<T>)
        {
            buffer.Write<T>(offset, stride, items_);
        }
        else
        {
            // A channel of indices has no vertex element form; the buffer keeps its zeros.
            (void)buffer;
            (void)offset;
            (void)stride;
        }
    }
}
