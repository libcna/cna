//
// Created by robertvokac on 6/5/25.
//

#include "System/Exception.h"

#include <iostream>

namespace System {
    Exception::Exception(const char * str) {
        std::cerr << str;
    }
} // System