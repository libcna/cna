//
// Created by robertvokac on 5/26/25.
//
#ifndef OVERFLOWEXCEPTION_H
#define OVERFLOWEXCEPTION_H
#include "System/ArithmeticException.h"

namespace System {
    class OverflowException : public System::ArithmeticException {
    public:
        explicit OverflowException(const char * str);

    };
} // System
#endif // OVERFLOWEXCEPTION_H
