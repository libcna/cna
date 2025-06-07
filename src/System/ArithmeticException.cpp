//
// Created by robertvokac on 6/5/25.
//

#include "System/ArithmeticException.h"

#include <iostream>

namespace System {
    ArithmeticException::ArithmeticException(const char * str): SystemException(str) {
        std::cerr << str;
    }
} // System