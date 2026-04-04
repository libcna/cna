//
// Created by robertvokac on 6/5/25.
//

#include "System/OverflowException.hpp"

#include <iostream>

namespace System {
    OverflowException::OverflowException(const char * str): ArithmeticException(str) {
        std::cerr << str;
    }
} // System