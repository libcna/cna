//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "Exception.hpp"

namespace System {
    class SystemException : public System::Exception {
    public: SystemException(const char * str);

    };
} // System
