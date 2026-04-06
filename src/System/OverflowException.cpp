//
// Created by robertvokac on 6/5/25.
//

#include "System/OverflowException.hpp"

namespace System {

    OverflowException::OverflowException(const char* str)
        : ArithmeticException(str) {
    }

} // namespace System