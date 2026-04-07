//
// Created by robertvokac on 6/7/25.
//

#include "System/InvalidOperationException.hpp"

namespace System {

    namespace {
        constexpr const char* DefaultInvalidOperationMessage =
            "Operation is not valid due to the current state of the object.";
    }

    InvalidOperationException::InvalidOperationException()
        : SystemException(DefaultInvalidOperationMessage) {
    }

    InvalidOperationException::InvalidOperationException(const char* message)
        : SystemException(message) {
    }

    InvalidOperationException::InvalidOperationException(const std::string& message)
        : SystemException(message) {
    }


} // namespace System