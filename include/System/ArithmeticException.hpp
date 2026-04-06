//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include "System/SystemException.hpp"
#include <string>

namespace System {

    /**
     * @brief Represents an exception that is thrown for errors in arithmetic,
     * casting, or conversion operations.
     *
     * This class is a C++ counterpart of the .NET System::ArithmeticException type.
     * It derives from System::SystemException and is used to report
     * arithmetic-related errors.
     */
    class ArithmeticException : public SystemException {
    public:
        /**
         * @brief Initializes a new instance of the ArithmeticException class
         * with an empty message.
         */
        ArithmeticException();

        /**
         * @brief Initializes a new instance of the ArithmeticException class
         * with the specified error message.
         *
         * @param str A null-terminated character string that describes the error.
         */
        explicit ArithmeticException(const char* str);

        /**
         * @brief Initializes a new instance of the ArithmeticException class
         * with the specified error message.
         *
         * @param str A string that describes the error.
         */
        explicit ArithmeticException(const std::string& str);
    };

} // namespace System