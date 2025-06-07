//
// Created by robertvokac on 5/26/25.
//
#ifndef CNAEXCEPTION_H
#define CNAEXCEPTION_H


#include "System/Exception.h"

namespace System {
    class CNAException : public System::Exception {
    public: CNAException(const char * str);

    };
} // System


#endif // CNAEXCEPTION_H
