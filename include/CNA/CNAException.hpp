//
// Created by robertvokac on 5/26/25.
//
#pragma once


#include "System/Exception.hpp"

namespace System {
    class CNAException : public System::Exception {
    public: CNAException(const char * str);

    };
} // System


