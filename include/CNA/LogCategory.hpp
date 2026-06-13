#pragma once

namespace CNA
{
    /** @brief Functional category of a log message, used to classify output by subsystem. */
    enum class LogCategory
    {
        /** @brief General application messages. */
        APPLICATION = 0,
        /** @brief Error-condition messages. */
        ERROR = 1,
        /** @brief Operating system or low-level system messages. */
        SYSTEM = 2,
        /** @brief Audio subsystem messages. */
        AUDIO = 3,
        /** @brief Video/media subsystem messages. */
        VIDEO = 4,
        /** @brief Graphics rendering messages. */
        RENDER = 5,
        /** @brief Input subsystem messages. */
        INPUT = 6,
        /** @brief Test and unit-test messages. */
        TEST = 7,
        /** @brief GPU device and driver messages. */
        GPU = 8,
    };
}
