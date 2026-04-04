//
// Created by robertvokac on 6/5/25.
//

#include "System/ArgumentException.hpp"

#include <iostream>

namespace System {
    ArgumentException::ArgumentException(const char * str): SystemException(str) {
        std::cerr << str;
    }
} // System