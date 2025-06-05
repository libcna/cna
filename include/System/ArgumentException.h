//
// Created by robertvokac on 5/26/25.
//
#ifndef ARGUMENTEXCEPTION_H
#define ARGUMENTEXCEPTION_H
#include "SystemException.h"

namespace System {
    class ArgumentException : public System::SystemException {
    public:
        explicit ArgumentException(const char * str);

    };
} // System


#endif // ARGUMENTEXCEPTION_H
