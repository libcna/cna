#pragma once

namespace CNA
{
    /** @brief Severity level of a log message. */
    enum class LogLevel
    {
        /** @brief Critical issue: key business functionalities are not working. */
        FATAL = 0,
        /** @brief One or more functionalities are not working properly. */
        ERROR = 1,
        /** @brief Unexpected behavior occurred, but the application continues. */
        WARN = 2,
        /** @brief Informational message about normal application events. */
        INFO = 3,
        /** @brief Useful for debugging and troubleshooting. */
        DEBUG = 4,
        /** @brief Fine-grained, highly detailed information for step-by-step tracing. */
        TRACE = 5,
        /** @brief Used for experimental features and test-related logging. */
        EXPERIMENT = 100
    };
}
