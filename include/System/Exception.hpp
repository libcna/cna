#pragma once
#include <exception>
#include <string>

namespace System {

    class Exception : public std::exception {
        std::string message;

    public:
        Exception() = default;
        explicit Exception(const char* msg);
        explicit Exception(const std::string& msg);
        [[nodiscard]] const char* what() const noexcept override;
    };

} // System