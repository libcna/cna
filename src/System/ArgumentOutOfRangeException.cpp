//
// Created by robertvokac on 6/5/25.
//

#include "System/ArgumentOutOfRangeException.h"

#include <iostream>

namespace System {
    ArgumentOutOfRangeException::ArgumentOutOfRangeException(const char * str): ArgumentException(str) {
        std::cerr << str;
    }
} // System