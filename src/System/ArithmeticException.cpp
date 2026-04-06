//
// Created by robertvokac on 6/5/25.
//

#include "System/ArithmeticException.hpp"

namespace System {

    /**
     * \brief Initializes a new instance of the ArithmeticException class
     * with an empty message.
     */
    ArithmeticException::ArithmeticException()
        : SystemException() {
    }

    /**
     * \brief Initializes a new instance of the ArithmeticException class
     * with the specified error message.
     * \param str A null-terminated character string that describes the error.
     */
    ArithmeticException::ArithmeticException(const char* str)
        : SystemException(str) {
    }

    /**
     * \brief Initializes a new instance of the ArithmeticException class
     * with the specified error message.
     * \param str A string that describes the error.
     */
    ArithmeticException::ArithmeticException(const std::string& str)
        : SystemException(str) {
    }

} // namespace System