//
// Created by robertvokac on 5/26/25.
//
#pragma once

#include "System/ArgumentException.hpp"
#include <string>

namespace System {

    /**
     * \class ArgumentOutOfRangeException
     * \brief Represents an exception that is thrown when the value of an argument
     * is outside the allowable range of values.
     *
     * This class is a C++ reimplementation of the .NET
     * System::ArgumentOutOfRangeException type. It derives from
     * ::System::ArgumentException and is used to report arguments whose values
     * are valid in form, but fall outside the permitted range.
     * @note Status: Partial
     */
    class ArgumentOutOfRangeException : public System::ArgumentException {
    public:
        /**
         * \brief Initializes a new instance of the ArgumentOutOfRangeException class
         * with an empty message.
         */
        ArgumentOutOfRangeException();

        /**
         * \brief Initializes a new instance of the ArgumentOutOfRangeException class
         * with the specified error message.
         * \param str A null-terminated character string that describes the error.
         */
        explicit ArgumentOutOfRangeException(const char* str);

        /**
         * \brief Initializes a new instance of the ArgumentOutOfRangeException class
         * with the specified error message.
         * \param str A string that describes the error.
         */
        explicit ArgumentOutOfRangeException(const std::string& str);
    };

} // namespace System