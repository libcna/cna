//
// Created by robertvokac on 6/5/25.
//

#include "System/Exception.h"

#include <iostream>

namespace System {
    Exception::Exception(const char * msg) : message(msg) {
        std::cerr << msg;
    }

    const char * Exception::what() const noexcept {
        return message.c_str();
    }
} // System