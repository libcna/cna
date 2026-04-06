//
// Written by robertvokac on 6/7/25.
//

#pragma once

namespace System {

    /**
     * @brief Defines a generalized comparison method for objects of the same type.
     *
     * This template is a C++ counterpart of the .NET IComparable<T> interface.
     * It allows a type to define its relative ordering with another instance
     * of the same type.
     *
     * The return value follows the usual .NET-style convention:
     * - negative value: this instance is less than @p other
     * - zero: this instance is equal to @p other
     * - positive value: this instance is greater than @p other
     *
     * @tparam T The type of object to compare with.
     */
    template <typename T>
    class IComparable {
    public:
        /**
         * @brief Compares the current instance with another instance of the same type.
         *
         * @param other Another object to compare with.
         * @return A negative value if this instance is less than @p other,
         *         0 if they are equal,
         *         or a positive value if this instance is greater than @p other.
         */
        virtual int CompareTo(const T& other) const = 0;

        /**
         * @brief Virtual destructor for safe polymorphic destruction.
         */
        virtual ~IComparable() = default;
    };

} // namespace System