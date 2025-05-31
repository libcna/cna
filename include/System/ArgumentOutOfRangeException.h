//
// Created by robertvokac on 5/26/25.
//
#ifndef ARGUMENTEXCEPTION_H
#define ARGUMENTEXCEPTION_H
#include "System/ArgumentException.h"

namespace System {
    class ArgumentOutOfRangeException : public System::ArgumentException {
        public:
        ArgumentOutOfRangeException() = default;

        explicit ArgumentOutOfRangeException(const char * str);
    };
} // System


#endif // ARGUMENTEXCEPTION_H
