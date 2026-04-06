//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include "System/ArithmeticException.hpp"

namespace System {

    /**
     * @brief Represents an exception that is thrown when an arithmetic,
     * casting, or conversion operation results in an overflow.
     *
     * This class is a C++ counterpart of the .NET System::OverflowException type.
     * It derives from System::ArithmeticException.
     */
    class OverflowException : public ArithmeticException {
    public:
        /**
         * @brief Initializes a new instance of the OverflowException class
         * with the specified error message.
         *
         * @param str A null-terminated character string that describes the error.
         */
        explicit OverflowException(const char* str);
    };

} // namespace System