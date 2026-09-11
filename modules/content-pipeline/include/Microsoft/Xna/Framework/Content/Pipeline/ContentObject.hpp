// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "System/InvalidCastException.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief The C++ spelling of `object` wherever the XNA pipeline API passes an untyped
     *        payload: an importer's or processor's `object` result, a processor's `object`
     *        input, and every `OpaqueDataDictionary` value
     *        (docs/xna-content-pipeline-compat-api.md §2).
     *
     * It is the canonical engine's own type-erased carrier, so an XNA-shaped component's output
     * is scheduled and written by the same registry as a native one, and it carries the stable
     * .NET type name the XNB writer needs to spell a reader.
     */
    CNAEXT using ContentObject = CNA::Content::Pipeline::ContentValue;

    /** @brief An `object` payload is named as `System.Object`. */
    template<>
    struct CNAEXT ContentTypeName<ContentObject>
    {
        /** @brief Returns `System.Object`. */
        [[nodiscard]] static std::string Name() { return "System.Object"; }
    };

    /**
     * @brief Boxes a typed carrier into a @ref ContentObject under its .NET type name.
     *
     * A reference type travels as its `std::shared_ptr<T>`; a value type is copied. The box
     * stores the carrier itself, so `Unbox<T>` on a reference type hands back the same shared
     * object, which is what lets an XNA-shaped processor mutate the graph it was given.
     *
     * @tparam T The XNA-shaped content type (not its carrier).
     * @param value The carrier to box.
     * @return A ContentObject whose stable type is `ContentTypeName<T>::Name()`.
     */
    template<typename T>
    [[nodiscard]] CNAEXT ContentObject Box(Carrier<T> value)
    {
        if constexpr (std::is_same_v<T, System::Object> || std::is_same_v<T, ContentObject>)
        {
            static_assert(!std::is_same_v<T, System::Object>,
                          "Box<System::Object>: an `object` payload is already a ContentObject; pass it through.");
        }
        return ContentObject::Create<Carrier<T>>(ContentTypeName<T>::Name(), std::move(value));
    }

    /**
     * @brief Unboxes a @ref ContentObject into the carrier of @p T.
     *
     * @tparam T The XNA-shaped content type (not its carrier).
     * @param value The boxed object.
     * @return The carrier stored in the box.
     * @throws System::InvalidCastException when the box is empty or holds another type -- the
     *         exception the `(T)value` cast raises in XNA.
     */
    template<typename T>
    [[nodiscard]] CNAEXT Carrier<T> Unbox(const ContentObject& value)
    {
        if (value.Empty())
        {
            throw System::InvalidCastException("Cannot unbox an empty content object as '" +
                                               ContentTypeName<T>::Name() + "'.");
        }
        try
        {
            return value.Get<Carrier<T>>();
        }
        catch (const std::logic_error& error)
        {
            throw System::InvalidCastException("Cannot unbox content of type '" + value.StableType() +
                                               "' as '" + ContentTypeName<T>::Name() + "': " +
                                               error.what());
        }
    }

    /**
     * @brief Returns whether a @ref ContentObject holds a value of type @p T.
     *
     * @tparam T The XNA-shaped content type (not its carrier).
     * @param value The boxed object.
     * @return True when the box is non-empty and its stable type is @p T's name.
     */
    template<typename T>
    [[nodiscard]] CNAEXT bool Holds(const ContentObject& value) noexcept
    {
        return !value.Empty() && value.StableType() == ContentTypeName<T>::Name();
    }
}
