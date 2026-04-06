//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "Exception.hpp"
#include <string>

namespace System {

    /**
     * \class SystemException
     * \brief Represents the base class for exceptions defined by the system.
     *
     * This class is a C++ reimplementation of the .NET System::SystemException type.
     * It derives from ::System::Exception and serves as a base class for exceptions
     * that are thrown by the runtime or framework itself.
     * @note Status: Partial
     */
    class SystemException : public System::Exception {
    public:
        /**
         * \brief Initializes a new instance of the SystemException class with an empty message.
         */
        SystemException();

        /**
         * \brief Initializes a new instance of the SystemException class with the specified message.
         * \param str A null-terminated character string that describes the error.
         */
        explicit SystemException(const char* str);

        /**
         * \brief Initializes a new instance of the SystemException class with the specified message.
         * \param str A string that describes the error.
         */
        explicit SystemException(const std::string& str);
    };

} // namespace System