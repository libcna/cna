//
// Created by robertvokac on 5/26/25.
//

#ifndef EXCEPTION_H
#define EXCEPTION_H
#include <exception>

namespace System {

class Exception : public std::exception {
public:
    Exception() = default;

    explicit Exception(const char * str);
};

} // System

#endif //EXCEPTION_H
