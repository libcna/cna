//
// Created by robertvokac on 6/5/25.
//

#include "System/SystemException.hpp"

namespace System {

    /**
     * \brief Initializes a new instance of the SystemException class with an empty message.
     * @note Status: Partial
     */
    SystemException::SystemException()
        : Exception() {
    }

    /**
     * \brief Initializes a new instance of the SystemException class with the specified message.
     * \param str A null-terminated character string that describes the error.
     */
    SystemException::SystemException(const char* str)
        : Exception(str) {
    }

    /**
     * \brief Initializes a new instance of the SystemException class with the specified message.
     * \param str A string that describes the error.
     */
    SystemException::SystemException(const std::string& str)
        : Exception(str) {
    }

} // namespace System