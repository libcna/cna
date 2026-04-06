//
// Created by robertvokac on 6/5/25.
//

#include "System/ArgumentOutOfRangeException.hpp"

namespace System {

    /**
     * \brief Initializes a new instance of the ArgumentOutOfRangeException class
     * with an empty message.
     */
    ArgumentOutOfRangeException::ArgumentOutOfRangeException()
        : ArgumentException() {
    }

    /**
     * \brief Initializes a new instance of the ArgumentOutOfRangeException class
     * with the specified error message.
     * \param str A null-terminated character string that describes the error.
     */
    ArgumentOutOfRangeException::ArgumentOutOfRangeException(const char* str)
        : ArgumentException(str) {
    }

    /**
     * \brief Initializes a new instance of the ArgumentOutOfRangeException class
     * with the specified error message.
     * \param str A string that describes the error.
     */
    ArgumentOutOfRangeException::ArgumentOutOfRangeException(const std::string& str)
        : ArgumentException(str) {
    }

} // namespace System