//
// Created by robertvokac on 5/26/25.
//
#ifndef ARITHMETICEXCEPTION_H
#define ARITHMETICEXCEPTION_H
#include "SystemException.h"

namespace System {
    class ArithmeticException : public System::SystemException {
    public:
        explicit ArithmeticException(const char * str);

    };
} // System
#endif // ARITHMETICEXCEPTION_H
