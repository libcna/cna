//
// Created by robertvokac on 5/26/25.
//
#pragma once

#include "System/ArgumentException.hpp"

namespace System {
    class ArgumentOutOfRangeException : public System::ArgumentException {
        public:
        ArgumentOutOfRangeException() = default;

        explicit ArgumentOutOfRangeException(const char * str);
    };
} // System
