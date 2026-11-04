// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <string>
#include <typeinfo>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "System/InvalidCastException.hpp"
#include "System/Object.hpp"

namespace CNA::Content
{
    /**
     * @brief A boxed content value: what a Model, ModelMesh or ModelMeshPart Tag holds when the
     *        serialized object is a string or a value rather than a System::Object.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. XNA's `Tag` is `object` and its ModelReader reads
     *       it with `ReadObject<object>` (Model.Read IL_0021-0028, Model.ReadMeshes IL_0045-004b,
     *       Model.ReadMeshParts IL_004d-0053), so any value an `.xnb` names arrives there boxed.
     *       CNA's `Tag` is a `System::Object*`; a value that is not one is carried in this box,
     *       keeping the type its own reader produced. Dictionaries keep arriving as
     *       ObjectDictionaryEXT.
     */
    class CNAEXT ObjectValueEXT : public System::Object
    {
    public:
        /**
         * @brief Boxes @p value.
         *
         * @param value The value, as its content type reader produced it.
         * @param typeName The .NET full name of the value's type, e.g. `System.String`.
         */
        ObjectValueEXT(std::any value, std::string typeName)
            : value_(std::move(value)), typeName_(std::move(typeName))
        {
        }

        /**
         * @brief Gets the .NET full name of the boxed value's type.
         *
         * @return The type name the box was created with.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override
        {
            return typeName_;
        }

        /**
         * @brief Unboxes the value as @p T, the C# cast.
         *
         * @tparam T The exact C++ type the value's reader produced.
         * @return The boxed value.
         * @throws System::InvalidCastException if the box holds another type.
         */
        template <typename T>
        [[nodiscard]] const T& Get() const
        {
            const T* typed = std::any_cast<T>(&value_);
            if (typed == nullptr)
            {
                throw System::InvalidCastException(
                    "ObjectValueEXT: the boxed " + typeName_ + " is not the requested type; it holds '" +
                    std::string(value_.type().name()) + "'.");
            }
            return *typed;
        }

        /**
         * @brief Gets the boxed value, type-erased.
         *
         * @return The value as its reader produced it.
         */
        [[nodiscard]] const std::any& getValueProperty() const
        {
            return value_;
        }

    private:
        std::any value_;
        std::string typeName_;
    };
}
