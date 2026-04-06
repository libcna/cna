//
// Created by robertvokac on 5/26/25.
//

#pragma once

#include "SystemException.hpp"
#include <string>

namespace System {

    /**
     * @class UnauthorizedAccessException
     * @brief Represents an exception that is thrown when access to a resource
     * or operation is denied.
     *
     * This class is a C++ counterpart of the .NET System::UnauthorizedAccessException
     * type. It derives from ::System::SystemException and is used to report
     * insufficient permissions or denied access to a requested resource or operation.
     */
    class UnauthorizedAccessException : public SystemException {
    public:
        /**
         * @brief Initializes a new instance of the UnauthorizedAccessException class
         * with an empty message.
         */
        UnauthorizedAccessException();

        /**
         * @brief Initializes a new instance of the UnauthorizedAccessException class
         * with the specified error message.
         *
         * @param str A null-terminated character string that describes the error.
         */
        explicit UnauthorizedAccessException(const char* str);

        /**
         * @brief Initializes a new instance of the UnauthorizedAccessException class
         * with the specified error message.
         *
         * @param str A string that describes the error.
         */
        explicit UnauthorizedAccessException(const std::string& str);
    };

} // namespace System