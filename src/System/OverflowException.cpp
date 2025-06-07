//
// Created by robertvokac on 6/5/25.
//

#include "System/OverflowException.h"

#include <iostream>

namespace System {
    OverflowException::OverflowException(const char * str): ArithmeticException(str) {
        std::cerr << str;
    }
} // System