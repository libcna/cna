//
// Created by robertvokac on 6/5/25.
//

#include "System/Exception.hpp"

namespace System {

    Exception::Exception()
        : message_("") {
    }

    Exception::Exception(const char* msg)
        : message_(msg ? msg : "") {
    }

    Exception::Exception(const std::string& msg)
        : message_(msg) {
    }

    const std::string& Exception::getMessageProperty() const {
        return message_;
    }

    const char* Exception::what() const noexcept {
        return message_.c_str();
    }

} // namespace System