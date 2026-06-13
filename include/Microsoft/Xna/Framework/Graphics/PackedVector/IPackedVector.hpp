// SPDX-License-Identifier: MS-PL
#pragma once

// C# has IPackedVector (non-generic) and IPackedVector<TPacked> (generic).
// C++ has no generics, so:
//   IPackedVector    — abstract base with PackFromVector4 (non-generic)
//   IPackedVectorT<T>— adds typed PackedValue get/set
// Inheriting virtual methods adds a vptr; for the common value-type case
// this is acceptable since these are only used through pointers for polymorphism.

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

// Forward declaration – Color.hpp (and anything else that includes this)
// will pull in Vector4 separately.
namespace Microsoft::Xna::Framework
{
    struct Vector4;
}

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Non-generic packed-vector interface.
     *
     * Mirrors C# @c Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.
     * The single method @c PackFromVector4 packs a normalized RGBA Vector4 into
     * the implementing type's packed storage.
     */
    struct IPackedVector
    {
        /**
         * @brief Packs a normalized RGBA color from a Vector4 into this object.
         * @param vector Normalized [0,1] RGBA components.
         */
        virtual void PackFromVector4(const Microsoft::Xna::Framework::Vector4& vector) = 0;

        /** @brief Virtual destructor. */
        NOXNA virtual ~IPackedVector() = default;
    };

    /**
     * @brief Generic packed-vector interface (mirrors C# @c IPackedVector<TPacked>).
     *
     * @tparam T  The packed storage type (e.g. @c SharpRuntime::UInt32 for Color).
     *
     * Inherits the non-generic @c IPackedVector so a @c Color* is implicitly an
     * @c IPackedVector* as well.
     */
    template <typename T>
    struct IPackedVectorT : public IPackedVector
    {
        /**
         * @brief Returns the packed representation of this color/vector.
         * Mirrors C# @c PackedValue { get; set; }.
         */
        [[nodiscard]] virtual T getPackedValueProperty() const = 0;

        /**
         * @brief Sets the packed representation of this color/vector.
         */
        virtual void setPackedValueProperty(T value) = 0;
    };
} // namespace Microsoft::Xna::Framework::Graphics::PackedVector
