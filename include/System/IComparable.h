//
// Written by robertvokac on 6/7/25.
//

#ifndef ICOMPARABLE_H
#define ICOMPARABLE_H

namespace System {

    /**
     * @brief Interface for types that define a custom ordering or sorting logic.
     *
     * This template allows a type to specify how it should be compared to another instance of the same type
     * in terms of relative order (less than, equal to, greater than).
     *
     * @tparam T The type of object to compare with.
     */
    template <typename T>
    class IComparable {
    public:
        /**
         * @brief Compares the current object with another object of the same type.
         *
         * @param other Pointer to another object to compare with.
         * @return A negative value if this is less than other, 0 if equal, or a positive value if greater.
         */
        virtual int CompareTo(const T& other) const = 0;

        /// Virtual destructor to support proper destruction via base class pointers.
        virtual ~IComparable() = default;
    };

} // namespace System

#endif // ICOMPARABLE_H
