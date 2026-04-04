//
// Created by robertvokac on 5/26/25.
//
#pragma once
#include "SystemException.hpp"

namespace System {
    class ArithmeticException : public System::SystemException {
    public:
        explicit ArithmeticException(const char * str);

    };
} // System
