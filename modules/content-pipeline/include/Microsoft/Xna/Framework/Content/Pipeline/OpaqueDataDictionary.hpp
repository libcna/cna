// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/NamedValueDictionary.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides properties that define opaque data for a game asset: importer-specific
     *        values a processor may consult, and a processor's own configuration when it is
     *        invoked through `ContentProcessorContext::Parameters`.
     *
     * Values are `object` in XNA and therefore @ref ContentObject here; `GetValue<T>` unboxes
     * them under the pipeline's type names.
     */
    class OpaqueDataDictionary final : public NamedValueDictionary<ContentObject>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary";

        /** @brief Initializes an empty dictionary. */
        OpaqueDataDictionary() = default;

        /**
         * @brief Gets the value associated with the specified key, or a default.
         *
         * @tparam T The expected value type (not its carrier).
         * @param key The key to look up.
         * @param defaultValue The value to return when the key is absent.
         * @return The stored value, or @p defaultValue.
         * @throws System::InvalidCastException when the stored value is not a @p T -- the
         *         `(T)value` cast XNA performs.
         */
        template<typename T>
        [[nodiscard]] Carrier<T> GetValue(const std::string& key, Carrier<T> defaultValue) const
        {
            ContentObject stored;
            if (!TryGetValue(key, stored)) { return defaultValue; }
            return Unbox<T>(stored);
        }

        /**
         * @brief Stores a typed value under a key, boxing it under its pipeline type name --
         *        the C# `dictionary[key] = value` idiom for an `object`-valued dictionary.
         *
         * @tparam T The value type (not its carrier).
         * @param key The key.
         * @param value The value to box and store.
         */
        template<typename T>
        CNAEXT void SetValue(const std::string& key, Carrier<T> value)
        {
            Set(key, Box<T>(std::move(value)));
        }

        /**
         * @brief Returns the contents of the dictionary as an XML fragment in the intermediate
         *        serialization format.
         *
         * @return XML text listing every entry as an `<Item>` with a `Key` and a typed `Value`.
         * @throws System::NotSupportedException until the intermediate serializer exists
         *         (plans/plan_xnapipeline_parity.md `XNAPP-072`); the parity map records this
         *         member as MISSING until then.
         */
        [[nodiscard]] std::string GetContentAsXml() const;

    protected:
        /**
         * @brief Gets the serializer type for values whose type cannot be inferred: `Object`.
         *
         * @return `System::Type::From<System::Object>()`.
         */
        [[nodiscard]] System::Type getDefaultSerializerTypeProperty() const override;

        /**
         * @brief Adds an element; an empty box is refused because XNA refuses null values here.
         *
         * @param key The key.
         * @param value The boxed value.
         * @throws System::ArgumentException for an empty key, a duplicate key or an empty box.
         */
        void AddItem(const std::string& key, const ContentObject& value) override;

        /** @brief Removes all elements. */
        void ClearItems() override;

        /**
         * @brief Removes the element with the specified key.
         *
         * @param key The key.
         * @return True when an entry was removed.
         */
        bool RemoveItem(const std::string& key) override;

        /**
         * @brief Replaces an element's value; an empty box is refused.
         *
         * @param key The key; must be present.
         * @param value The boxed value.
         * @throws System::ArgumentException for an empty box.
         */
        void SetItem(const std::string& key, const ContentObject& value) override;
    };
}
