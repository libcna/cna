//
// IPackedVector.hpp — C++ port of the FNA/XNA IPackedVector interfaces.
//
// FNA original:
//   namespace Microsoft.Xna.Framework.Graphics.PackedVector
//   public interface IPackedVector
//   public interface IPackedVector<TPacked>
//
// In C# Color implements both IPackedVector (non-generic) and
// IPackedVector<uint> (generic).  C++ has no generics, so we use:
//   - IPackedVector          — abstract base with PackFromVector4 (non-generic)
//   - IPackedVectorT<T>      — CRTP / template base that additionally
//                              exposes PackedValue get/set typed to T
// Color inherits IPackedVectorT<UInt32> which itself inherits IPackedVector.
//
// Note: Color is a value-type (struct) in C#.  Inheriting from a base class
// in C++ with virtual methods adds a vptr and changes the ABI / size.  To
// keep the common case zero-overhead the base classes are header-only and
// use CRTP where possible.  Any class that actually needs runtime polymorphism
// via IPackedVector* can still do so through the virtual interface.

#pragma once

#include "SharpRuntime/SharpRuntimeHelper.hpp"

// Forward declaration – Color.hpp (and anything else that includes this)
// will pull in Vector4 separately.
namespace Microsoft::Xna::Framework { struct Vector4; }

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

        virtual ~IPackedVector() = default;
    };

    /**
     * @brief Generic packed-vector interface (mirrors C# @c IPackedVector<TPacked>).
     *
     * @tparam T  The packed storage type (e.g. @c SharpRuntime::UInt32 for Color).
     *
     * Inherits the non-generic @c IPackedVector so a @c Color* is implicitly an
     * @c IPackedVector* as well.
     */
    template<typename T>
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
