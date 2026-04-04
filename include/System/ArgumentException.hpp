//
// Created by robertvokac on 5/26/25.
//
#pragma once
#include "SystemException.hpp"

namespace System {
    class ArgumentException : public System::SystemException {
    public:
        explicit ArgumentException(const char * str);

    };
} // System


