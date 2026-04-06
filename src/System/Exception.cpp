#include "System/Exception.hpp"
#include <SDL3/SDL_log.h>

namespace System {

    /**
     * \brief Initializes a new instance of the Exception class with the specified message.
     * \param msg A null-terminated character string that describes the error.
     *
     * If \p msg is null, an empty message is stored instead.
     * The message is also written to the SDL application error log.
     */
    Exception::Exception(const char* msg)
        : message(msg ? msg : "")
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
    }

    /**
     * \brief Initializes a new instance of the Exception class with the specified message.
     * \param msg A string that describes the error.
     *
     * The message is also written to the SDL application error log.
     */
    Exception::Exception(const std::string& msg)
        : message(msg)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
    }

    /**
     * \brief Returns the explanatory message associated with this exception.
     * \return A pointer to a null-terminated character sequence describing the error.
     */
    const char* Exception::what() const noexcept {
        return message.c_str();
    }

} // namespace System