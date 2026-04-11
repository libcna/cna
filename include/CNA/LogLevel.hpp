#pragma once

namespace CNA
{
    /**
     * @brief Severity level of a log message.
     */
    enum class LogLevel
    {
        FATAL = 0,      ///< Critical issue: key business functionalities are not working.
        ERROR = 1,      ///< One or more functionalities are not working properly.
        WARN = 2,       ///< Unexpected behavior occurred, but the application continues.
        INFO = 3,       ///< Informational message about normal application events.
        DEBUG = 4,      ///< Useful for debugging and troubleshooting.
        TRACE = 5,      ///< Fine-grained, highly detailed information for step-by-step tracing.
        EXPERIMENT = 100 ///< Used for experimental features and test-related logging.
    };
}