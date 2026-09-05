// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    struct Vector2;
    struct Vector3;
    struct Vector4;
    struct Matrix;
    struct Quaternion;
    struct Color;
    struct Rectangle;
    struct Point;
    struct BoundingBox;
    struct BoundingSphere;
    class BoundingFrustum;
    struct Ray;
    struct Plane;
    class Curve;
}

namespace System
{
    class Object;
    struct TimeSpan;
    struct DateTime;
    struct Decimal;
}

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Supplies the .NET-style full type name every pipeline value is identified by
     *        (plans/plan_xnapipeline_parity.md `XNAPP-030`, docs/xna-content-pipeline-compat-api.md §3).
     *
     * XNA identifies content by CLR type; C++ has no runtime type name that survives compilers,
     * so the name is supplied explicitly. The primary template reads a class's
     * `static constexpr std::string_view XnaTypeName`; primitives, framework value types and the
     * containers have specializations below. A game type either declares `XnaTypeName` or
     * specializes this trait -- the same obligation XNA's `ContentTypeWriter<T>::GetRuntimeType`
     * places on a custom type.
     *
     * @tparam T The C++ type whose pipeline name is wanted.
     */
    template<typename T>
    struct CNAEXT ContentTypeName
    {
        /**
         * @brief Returns the stable .NET-style full name of @p T.
         *
         * @return The declared `XnaTypeName` of the class.
         */
        [[nodiscard]] static std::string Name()
        {
            static_assert(requires { { T::XnaTypeName } -> std::convertible_to<std::string_view>; },
                          "ContentTypeName<T>: declare `static constexpr std::string_view XnaTypeName` "
                          "on the type or specialize ContentTypeName<T>.");
            return std::string(T::XnaTypeName);
        }
    };

    /** @brief A reference-typed value travels as a shared pointer; its name is the pointee's. */
    template<typename T>
    struct CNAEXT ContentTypeName<std::shared_ptr<T>>
    {
        /** @brief Returns the pointee type's name. */
        [[nodiscard]] static std::string Name() { return ContentTypeName<T>::Name(); }
    };

    /** @brief A shared pointer to const carries the same name as the mutable one. */
    template<typename T>
    struct CNAEXT ContentTypeName<std::shared_ptr<const T>>
    {
        /** @brief Returns the pointee type's name. */
        [[nodiscard]] static std::string Name() { return ContentTypeName<T>::Name(); }
    };

    /** @brief `System.Collections.Generic.List`1[[T]]`, the spelling XNA uses for lists and arrays' lists. */
    template<typename T>
    struct CNAEXT ContentTypeName<std::vector<T>>
    {
        /** @brief Returns the generic list name with its argument spelled by its own trait. */
        [[nodiscard]] static std::string Name()
        {
            return "System.Collections.Generic.List`1[[" + ContentTypeName<T>::Name() + "]]";
        }
    };

    /** @brief `System.Nullable`1[[T]]`. */
    template<typename T>
    struct CNAEXT ContentTypeName<std::optional<T>>
    {
        /** @brief Returns the nullable name with its argument spelled by its own trait. */
        [[nodiscard]] static std::string Name()
        {
            return "System.Nullable`1[[" + ContentTypeName<T>::Name() + "]]";
        }
    };

    /** @brief `System.Collections.Generic.Dictionary`2[[K],[V]]`. */
    template<typename K, typename V>
    struct CNAEXT ContentTypeName<std::map<K, V>>
    {
        /** @brief Returns the generic dictionary name with both arguments spelled by their traits. */
        [[nodiscard]] static std::string Name()
        {
            return "System.Collections.Generic.Dictionary`2[[" + ContentTypeName<K>::Name() + "],[" +
                   ContentTypeName<V>::Name() + "]]";
        }
    };

/// Declares a ContentTypeName specialization for one concrete type.
#define CNA_XNA_CONTENT_TYPE_NAME(cppType, dotNetName)                                            \
    template<>                                                                                    \
    struct CNAEXT ContentTypeName<cppType>                                                        \
    {                                                                                             \
        /** @brief Returns the fixed .NET name of this type. */                                   \
        [[nodiscard]] static std::string Name() { return dotNetName; }                            \
    }

    CNA_XNA_CONTENT_TYPE_NAME(bool, "System.Boolean");
    CNA_XNA_CONTENT_TYPE_NAME(std::int8_t, "System.SByte");
    CNA_XNA_CONTENT_TYPE_NAME(std::uint8_t, "System.Byte");
    CNA_XNA_CONTENT_TYPE_NAME(std::int16_t, "System.Int16");
    CNA_XNA_CONTENT_TYPE_NAME(std::uint16_t, "System.UInt16");
    CNA_XNA_CONTENT_TYPE_NAME(std::int32_t, "System.Int32");
    CNA_XNA_CONTENT_TYPE_NAME(std::uint32_t, "System.UInt32");
    CNA_XNA_CONTENT_TYPE_NAME(std::int64_t, "System.Int64");
    CNA_XNA_CONTENT_TYPE_NAME(std::uint64_t, "System.UInt64");
    CNA_XNA_CONTENT_TYPE_NAME(float, "System.Single");
    CNA_XNA_CONTENT_TYPE_NAME(double, "System.Double");
    CNA_XNA_CONTENT_TYPE_NAME(char16_t, "System.Char");
    CNA_XNA_CONTENT_TYPE_NAME(std::string, "System.String");
    CNA_XNA_CONTENT_TYPE_NAME(System::Object, "System.Object");
    CNA_XNA_CONTENT_TYPE_NAME(System::TimeSpan, "System.TimeSpan");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Vector2, "Microsoft.Xna.Framework.Vector2");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Vector3, "Microsoft.Xna.Framework.Vector3");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Vector4, "Microsoft.Xna.Framework.Vector4");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Matrix, "Microsoft.Xna.Framework.Matrix");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Quaternion, "Microsoft.Xna.Framework.Quaternion");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Color, "Microsoft.Xna.Framework.Color");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Rectangle, "Microsoft.Xna.Framework.Rectangle");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Point, "Microsoft.Xna.Framework.Point");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::BoundingBox, "Microsoft.Xna.Framework.BoundingBox");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::BoundingSphere, "Microsoft.Xna.Framework.BoundingSphere");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Ray, "Microsoft.Xna.Framework.Ray");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Plane, "Microsoft.Xna.Framework.Plane");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::Curve, "Microsoft.Xna.Framework.Curve");
    CNA_XNA_CONTENT_TYPE_NAME(Microsoft::Xna::Framework::BoundingFrustum, "Microsoft.Xna.Framework.BoundingFrustum");
    CNA_XNA_CONTENT_TYPE_NAME(System::DateTime, "System.DateTime");
    CNA_XNA_CONTENT_TYPE_NAME(System::Decimal, "System.Decimal");

    /**
     * @brief Selects how a value of type @p T travels through the pipeline
     *        (docs/xna-content-pipeline-compat-api.md §2).
     *
     * A .NET reference type is shared and mutated in place by processors, so anything deriving
     * `System::Object` travels as `std::shared_ptr<T>`; everything else travels by value.
     *
     * @tparam T The XNA-shaped content type.
     */
    template<typename T>
    struct CNAEXT ContentCarrier
    {
        /** @brief `std::shared_ptr<T>` for reference types, `T` otherwise. */
        using type = std::conditional_t<std::is_base_of_v<System::Object, T>, std::shared_ptr<T>, T>;
    };

    /** @brief A type already spelled as a shared pointer travels as itself. */
    template<typename T>
    struct CNAEXT ContentCarrier<std::shared_ptr<T>>
    {
        /** @brief The shared pointer itself. */
        using type = std::shared_ptr<T>;
    };

    /** @brief Convenience alias for @ref ContentCarrier. */
    /** @brief .NET value types travel by value even where sharp-runtime models them as objects. */
    template<>
    struct CNAEXT ContentCarrier<System::DateTime>
    {
        using type = System::DateTime;
    };

    /** @brief .NET value types travel by value even where sharp-runtime models them as objects. */
    template<>
    struct CNAEXT ContentCarrier<System::TimeSpan>
    {
        using type = System::TimeSpan;
    };

    /** @brief .NET value types travel by value even where sharp-runtime models them as objects. */
    template<>
    struct CNAEXT ContentCarrier<System::Decimal>
    {
        using type = System::Decimal;
    };

    template<typename T>
    using Carrier = typename ContentCarrier<T>::type;
}
