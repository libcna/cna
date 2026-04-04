//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include <exception>
#include <string>

namespace System {

class Exception : public std::exception {
private:
    std::string message;
public:
    Exception() = default;

    explicit Exception(const char * msg);
    const char* what() const noexcept override;
};

} // System

