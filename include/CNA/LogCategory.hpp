#pragma once

namespace CNA
{
    /**
     * @brief Functional category of a log message.
     */
    enum class LogCategory
    {
        APPLICATION = 0,
        ERROR = 1,
        SYSTEM = 2,
        AUDIO = 3,
        VIDEO = 4,
        RENDER = 5,
        INPUT = 6,
        TEST = 7,
        GPU = 8,
    };
}