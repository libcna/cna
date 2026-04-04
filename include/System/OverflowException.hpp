//
// Created by robertvokac on 5/26/25.
//
#pragma once
#include "System/ArithmeticException.hpp"

namespace System {
    class OverflowException : public System::ArithmeticException {
    public:
        explicit OverflowException(const char * str);

    };
} // System
