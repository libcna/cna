//
// Created by robertvokac on 5/26/25.
//
#ifndef ARGUMENTOUTOFRANGEEXCEPTION_H
#define ARGUMENTOUTOFRANGEEXCEPTION_H

#include "System/ArgumentException.h"

namespace System {
    class ArgumentOutOfRangeException : public System::ArgumentException {
        public:
        ArgumentOutOfRangeException() = default;

        explicit ArgumentOutOfRangeException(const char * str);
    };
} // System


#endif // ARGUMENTOUTOFRANGEEXCEPTION_H
