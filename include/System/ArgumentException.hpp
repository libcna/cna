//
// Created by robertvokac on 5/26/25.
//
#pragma once
#include "SystemException.hpp"
#include <string>

namespace System {

    /**
     * \class ArgumentException
     * \brief Represents an exception that is thrown when one of the arguments
     * provided to a method is not valid.
     *
     * This class is a C++ reimplementation of the .NET System::ArgumentException
     * type. It derives from ::System::SystemException and is used to report
     * invalid method arguments.
     */
    class ArgumentException : public System::SystemException {
    public:
        /**
         * \brief Initializes a new instance of the ArgumentException class
         * with an empty message.
         */
        ArgumentException();

        /**
         * \brief Initializes a new instance of the ArgumentException class
         * with the specified error message.
         * \param str A null-terminated character string that describes the error.
         */
        explicit ArgumentException(const char* str);

        /**
         * \brief Initializes a new instance of the ArgumentException class
         * with the specified error message.
         * \param str A string that describes the error.
         */
        explicit ArgumentException(const std::string& str);
    };

} // namespace System