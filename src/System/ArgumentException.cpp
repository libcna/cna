//
// Created by robertvokac on 6/5/25.
//

#include "System/ArgumentException.hpp"

namespace System {

    /**
     * \brief Initializes a new instance of the ArgumentException class
     * with an empty message.
     */
    ArgumentException::ArgumentException()
        : SystemException() {
    }

    /**
     * \brief Initializes a new instance of the ArgumentException class
     * with the specified error message.
     * \param str A null-terminated character string that describes the error.
     */
    ArgumentException::ArgumentException(const char* str)
        : SystemException(str) {
    }

    /**
     * \brief Initializes a new instance of the ArgumentException class
     * with the specified error message.
     * \param str A string that describes the error.
     */
    ArgumentException::ArgumentException(const std::string& str)
        : SystemException(str) {
    }

} // namespace System