// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureReferenceDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides methods and properties for maintaining a collection of named texture
     *        references and shader properties: the base of every material a model carries.
     *
     * A material has no fields of its own. Every typed property of a derived material is a view
     * over two dictionaries -- `OpaqueData` for values and references, `Textures` for texture
     * references -- which is why setting a property to null removes its entry rather than storing
     * a null (measured, tests/reference/xna40/graphics case material/basic_property_cleared).
     */
    class MaterialContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent";

        /** @brief Initializes a new instance of MaterialContent. */
        MaterialContent() = default;

        /**
         * @brief Gets the texture collection of the material.
         *
         * @return The named external texture references.
         */
        [[nodiscard]] TextureReferenceDictionary& getTexturesProperty() noexcept;

        /**
         * @brief Gets the texture collection of the material.
         *
         * @return The named external texture references.
         */
        [[nodiscard]] const TextureReferenceDictionary& getTexturesProperty() const noexcept;

        /**
         * @brief Describes the material for the intermediate serializer: the members of
         *        ContentItem, then the texture collection, each omitted while it is empty.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<MaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of the material type, as XNA's `ToString` does.
         *
         * @return The .NET full name of the concrete material type.
         */
        [[nodiscard]] std::string ToString() const;

    protected:
        /**
         * @brief Gets a reference-typed property from the opaque data.
         *
         * @tparam T The expected type.
         * @param key The property name.
         * @return The stored reference, or null when the property is absent or holds another
         *         type -- the runtime answers null rather than refusing (measured,
         *         material/read_reference_wrong_type).
         */
        template<typename T>
        [[nodiscard]] std::shared_ptr<T> GetReferenceTypeProperty(const std::string& key) const
        {
            ContentObject stored;
            if (!getOpaqueDataProperty().TryGetValue(key, stored) || !Holds<std::shared_ptr<T>>(stored))
            {
                return nullptr;
            }
            return Unbox<std::shared_ptr<T>>(stored);
        }

        /**
         * @brief Gets a named external texture reference.
         *
         * @param key The texture slot name.
         * @return The reference, or null when the slot is empty.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> GetTexture(const std::string& key) const;

        /**
         * @brief Gets a value-typed property from the opaque data.
         *
         * @tparam T The expected value type.
         * @param key The property name.
         * @return The stored value, or an empty optional when the property is absent or holds
         *         another type (measured, material/read_value_wrong_type).
         */
        template<typename T>
        [[nodiscard]] std::optional<T> GetValueTypeProperty(const std::string& key) const
        {
            ContentObject stored;
            if (!getOpaqueDataProperty().TryGetValue(key, stored) || !Holds<T>(stored))
            {
                return std::nullopt;
            }
            return Unbox<T>(stored);
        }

        /**
         * @brief Stores a property in the opaque data, or removes it when the value is null.
         *
         * @tparam T The value's carrier: `std::optional<U>` for a value type, `std::shared_ptr<U>`
         *         for a reference type, or the value itself.
         * @param key The property name.
         * @param value The value to store; an empty optional or a null pointer removes the entry.
         * @throws System::ArgumentNullException when the key is empty.
         */
        template<typename T>
        void SetProperty(const std::string& key, const T& value)
        {
            RequireKey(key);
            if constexpr (requires { static_cast<bool>(value); })
            {
                if (!static_cast<bool>(value))
                {
                    getOpaqueDataProperty().Remove(key);
                    return;
                }
            }
            if constexpr (Serialization::Intermediate::detail::IsOptional<T>::value)
            {
                getOpaqueDataProperty().template SetValue<typename T::value_type>(key, *value);
            }
            else
            {
                getOpaqueDataProperty().template SetValue<T>(key, value);
            }
        }

        /**
         * @brief Stores a named external texture reference, or removes it when the reference is
         *        null.
         *
         * @param key The texture slot name.
         * @param value The reference to store; null removes the slot.
         * @throws System::ArgumentNullException when the key is empty.
         */
        void SetTexture(const std::string& key, const std::shared_ptr<ExternalReference<TextureContent>>& value);

    private:
        /** @brief Refuses an empty key with the message the runtime gives for a null one. */
        static void RequireKey(const std::string& key);

        TextureReferenceDictionary textures_;
    };
}
