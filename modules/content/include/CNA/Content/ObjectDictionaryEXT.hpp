// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <map>
#include <string>
#include <typeinfo>

#include "CNA/CNAHelper.hpp"
#include "System/InvalidCastException.hpp"
#include "System/Object.hpp"

namespace CNA::Content
{
    /**
     * @brief CNAEXT carrier for a string-keyed dictionary read out of an `.xnb`.
     *
     * This is the shape a custom `ContentProcessor` uses to hand a game data the stock pipeline
     * has no type for: it attaches a `Dictionary<string, object>` to `Model.Tag`, and the game
     * casts it back. XNA's own samples do it — `TrianglePickingSample`'s processor tags every
     * model with its world-space triangle vertices and a `BoundingSphere` that way.
     *
     * C++ has no `object`, so the cast the C# performs
     *
     * ```csharp
     * Dictionary<string, object> tagData = (Dictionary<string, object>)model.Tag;
     * BoundingSphere sphere = (BoundingSphere)tagData["BoundingSphere"];
     * ```
     *
     * becomes a `dynamic_cast` to this type followed by @ref Get:
     *
     * ```cpp
     * auto* tagData = dynamic_cast<CNA::Content::ObjectDictionaryEXT*>(model.getTagProperty());
     * const auto sphere = tagData->Get<BoundingSphere>("BoundingSphere");
     * ```
     *
     * Each value keeps whatever type its own content type reader produced, so the element types
     * are the reader's, not this class's: a `Vector3[]` or `List<Vector3>` arrives as
     * `std::vector<Vector3>`, a `BoundingSphere` as `BoundingSphere`. The same carrier also keeps
     * a statically typed dictionary such as `Dictionary<string, List<Vector3>>`: its entries are
     * erased only after the closed generic reader has enforced their one common value type. @ref
     * Get names that C++ type and throws `System::InvalidCastException` when it is wrong, which is
     * what the C# cast does.
     */
    class CNAEXT ObjectDictionaryEXT : public System::Object
    {
    public:
        /**
         * @brief Takes ownership of the values a `DictionaryReader<String, Object>` produced.
         * @param values The deserialized entries, keyed by their `.xnb` names.
         */
        explicit ObjectDictionaryEXT(std::map<std::string, std::any> values)
            : values_(std::move(values))
        {
        }

        /**
         * @brief Takes ownership of a string-keyed dictionary and its logical runtime type name.
         *
         * This overload is used for closed generic dictionaries whose value type is not
         * `System.Object`. The reader converts each already-type-checked value to `std::any`, while
         * the supplied name preserves the original managed dictionary type exposed by `Model.Tag`.
         *
         * @param values The deserialized entries, keyed by their `.xnb` names.
         * @param typeName The fully qualified logical managed type name.
         */
        ObjectDictionaryEXT(std::map<std::string, std::any> values, std::string typeName)
            : values_(std::move(values)), typeName_(std::move(typeName))
        {
        }

        /**
         * @brief Returns the fully qualified logical type name of this object.
         * @return The managed dictionary type retained by the content reader.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override
        {
            return typeName_;
        }

        /**
         * @brief Gets whether an entry with this key exists.
         * @param key The entry's name, as the processor wrote it.
         * @return True when the entry is present.
         */
        [[nodiscard]] bool ContainsKey(const std::string& key) const
        {
            return values_.find(key) != values_.end();
        }

        /**
         * @brief Gets one entry, typed.
         *
         * @tparam T The C++ type the entry's own content type reader produced.
         * @param key The entry's name, as the processor wrote it.
         * @return A const reference to the stored value.
         * @throws System::KeyNotFoundException when no entry has that key.
         * @throws System::InvalidCastException when the entry holds a different type, which is
         *         what the C# `(T)dictionary[key]` cast raises.
         */
        template <typename T>
        [[nodiscard]] const T& Get(const std::string& key) const
        {
            const std::any& value = At(key);
            const T* typed = std::any_cast<T>(&value);
            if (typed == nullptr)
            {
                throw System::InvalidCastException(
                    "ObjectDictionaryEXT: entry '" + key + "' does not hold the requested type; "
                    "it holds '" + std::string(value.type().name()) + "'.");
            }
            return *typed;
        }

        /**
         * @brief Gets one entry without naming its type.
         * @param key The entry's name.
         * @return A const reference to the stored value.
         * @throws System::KeyNotFoundException when no entry has that key.
         */
        [[nodiscard]] const std::any& operator[](const std::string& key) const { return At(key); }

        /**
         * @brief Gets every entry.
         * @return The entries, keyed by their `.xnb` names.
         */
        [[nodiscard]] const std::map<std::string, std::any>& getValuesProperty() const
        {
            return values_;
        }

    private:
        [[nodiscard]] const std::any& At(const std::string& key) const;

        std::map<std::string, std::any> values_;
        std::string typeName_ =
            "System.Collections.Generic.Dictionary`2[System.String,System.Object]";
    };
}
