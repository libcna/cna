//
// Written by robertvokac on 6/7/25.
//

#pragma once

namespace System {

    /**
     * @brief Interface for types that provide a custom way to compare objects for equality.
     *
     * This template allows a class or struct to define how it should be compared to another instance of the same type.
     *
     * @tparam T The type of object to compare with.
     */
    template <typename T>
    class IEquatable {
    public:
        /**
         * @brief Checks if this instance is considered equal to another instance.
         *
         * @param other Pointer to another object of the same type.
         * @return true if both instances are considered equal, false otherwise.
         */
        virtual bool Equals(const T& other) const = 0;

        /// Virtual destructor to ensure proper cleanup in derived classes.
        virtual ~IEquatable() = default;
    };

} // namespace System

