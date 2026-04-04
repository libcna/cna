#include "System/Exception.hpp"
#include <SDL3/SDL_log.h>

namespace System {

    Exception::Exception(const char* msg)
        : message(msg ? msg : "")
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
    }

    Exception::Exception(const std::string& msg)
        : message(msg)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
    }

    const char* Exception::what() const noexcept {
        return message.c_str();
    }

} // System