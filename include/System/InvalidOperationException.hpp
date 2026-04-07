//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include <string>

#include "System/SystemException.hpp"

namespace System {

    /**
     * @brief The exception that is thrown when a method call is invalid
     * for the object's current state.
     *
     * This is a partial C++ counterpart of the .NET
     * System::InvalidOperationException type.
     *
     * @note Status: Partial.
     * @note HResult, serialization, and inner exception support
     * are not implemented here.
     */
    class InvalidOperationException : public SystemException {
    public:
        /**
         * @brief Initializes a new instance of the InvalidOperationException class
         * with a default message.
         */
        InvalidOperationException();

        /**
         * @brief Initializes a new instance of the InvalidOperationException class
         * with the specified error message.
         *
         * @param message A null-terminated character string that describes the error.
         */
        explicit InvalidOperationException(const char* message);

        /**
         * @brief Initializes a new instance of the InvalidOperationException class
         * with the specified error message.
         *
         * @param message A string that describes the error.
         */
        explicit InvalidOperationException(const std::string& message);

    };

} // namespace System