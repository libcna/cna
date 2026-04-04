//
// Created by robertvokac on 6/5/25.
//

#include "CNA/CNAException.hpp"

#include <iostream>

namespace System {
    CNAException::CNAException(const char * str): Exception(str) {
        std::cerr << str;
    }
} // System