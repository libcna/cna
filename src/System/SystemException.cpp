//
// Created by robertvokac on 6/5/25.
//

#include "System/SystemException.h"

#include <iostream>

namespace System {
    SystemException::SystemException(const char * str): Exception(str) {
        std::cerr << str;
    }
} // System