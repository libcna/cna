// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/PixelTraits.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Collections/ObjectModel/Collection.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief The vertex layout a vertex buffer is written with: its elements and their stride.
     */
    class VertexDeclarationContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.VertexDeclarationContent";

        /** @brief Initializes an empty declaration, whose stride is not yet decided. */
        VertexDeclarationContent() = default;

        /**
         * @brief Gets the elements of one vertex.
         *
         * @return The elements, in the order they are written.
         */
        [[nodiscard]] System::Collections::ObjectModel::Collection<
            Microsoft::Xna::Framework::Graphics::VertexElement>&
        getVertexElementsProperty() noexcept;

        /**
         * @brief Gets the elements of one vertex.
         *
         * @return The elements, in the order they are written.
         */
        [[nodiscard]] const System::Collections::ObjectModel::Collection<
            Microsoft::Xna::Framework::Graphics::VertexElement>&
        getVertexElementsProperty() const noexcept;

        /**
         * @brief Gets the number of bytes one vertex occupies.
         *
         * @return The stride, or an empty optional when it has not been set -- which is what a
         *         fresh declaration answers (measured,
         *         `tests/reference/xna40/graphics/graphics-content-oracle.json`,
         *         `modelprocessor/vertex_declaration_defaults`).
         */
        [[nodiscard]] std::optional<SharpRuntime::intcs> getVertexStrideProperty() const noexcept;

        /**
         * @brief Sets the number of bytes one vertex occupies.
         *
         * @param value The stride, or an empty optional to leave it undecided.
         */
        void setVertexStrideProperty(std::optional<SharpRuntime::intcs> value) noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        System::Collections::ObjectModel::Collection<Microsoft::Xna::Framework::Graphics::VertexElement> elements_;
        std::optional<SharpRuntime::intcs> stride_;
    };

    /**
     * @brief The bytes of one vertex buffer, together with the layout they follow.
     */
    class VertexBufferContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.VertexBufferContent";

        /** @brief Initializes an empty buffer with an empty declaration. */
        VertexBufferContent();

        /**
         * @brief Initializes a buffer of the given size, in bytes.
         *
         * @param size How many bytes the buffer holds.
         */
        CNAEXT explicit VertexBufferContent(SharpRuntime::intcs size);

        /**
         * @brief Gets the bytes of the buffer.
         *
         * @return The vertex data.
         */
        [[nodiscard]] const std::vector<SharpRuntime::bytecs>& getVertexDataProperty() const noexcept;

        /**
         * @brief Gets the bytes of the buffer for modification.
         *
         * @return The vertex data.
         */
        CNAEXT [[nodiscard]] std::vector<SharpRuntime::bytecs>& getVertexDataProperty() noexcept;

        /**
         * @brief Gets the layout the buffer follows.
         *
         * @return The declaration, which is never null.
         */
        [[nodiscard]] const std::shared_ptr<VertexDeclarationContent>& getVertexDeclarationProperty() const noexcept;

        /**
         * @brief Sets the layout the buffer follows.
         *
         * @param value The declaration.
         */
        void setVertexDeclarationProperty(std::shared_ptr<VertexDeclarationContent> value) noexcept;

        /**
         * @brief Gets how many bytes one value of the given vertex element type occupies.
         *
         * @param type The element type.
         * @return The size in bytes.
         * @throws System::ArgumentNullException when the type is the null type.
         * @throws System::NotSupportedException when the type is not a vertex element type.
         */
        [[nodiscard]] static SharpRuntime::intcs SizeOf(System::Type type);

        /**
         * @brief Writes a sequence of values into the buffer, one per vertex.
         *
         * @tparam T The value type.
         * @param offset The byte offset of the first value.
         * @param stride The number of bytes between values.
         * @param data The values to write.
         * @throws System::ArgumentOutOfRangeException when a value would fall outside the buffer.
         */
        template<typename T>
        void Write(SharpRuntime::intcs offset, SharpRuntime::intcs stride, const std::vector<T>& data)
        {
            static_assert(Graphics::detail::ValidPixelType<T>,
                          "VertexBufferContent::Write<T>: T must be a vertex element type.");
            std::vector<SharpRuntime::bytecs> bytes(Graphics::detail::PixelTraits<T>::Bytes);
            for (std::size_t i = 0; i < data.size(); ++i)
            {
                Graphics::detail::PixelTraits<T>::Write(data[i], bytes.data());
                WriteBytes(offset + static_cast<SharpRuntime::intcs>(i) * stride, bytes);
            }
        }

        /**
         * @brief Writes a sequence of boxed values of one element type into the buffer.
         *
         * @param offset The byte offset of the first value.
         * @param stride The number of bytes between values.
         * @param dataType The element type every value has.
         * @param data The values to write.
         * @throws System::NotSupportedException when the type is not a vertex element type.
         * @throws System::ArgumentException when a value is not of that type.
         * @throws System::ArgumentOutOfRangeException when a value would fall outside the buffer.
         */
        void Write(SharpRuntime::intcs offset, SharpRuntime::intcs stride, System::Type dataType,
                   const std::vector<ContentObject>& data);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        /**
         * @brief Copies bytes into the buffer, growing nothing.
         *
         * @param offset The byte offset to write at.
         * @param bytes The bytes to write.
         * @throws System::ArgumentOutOfRangeException when they would fall outside the buffer.
         */
        void WriteBytes(SharpRuntime::intcs offset, const std::vector<SharpRuntime::bytecs>& bytes);

        std::vector<SharpRuntime::bytecs> data_;
        std::shared_ptr<VertexDeclarationContent> declaration_;
    };
}
