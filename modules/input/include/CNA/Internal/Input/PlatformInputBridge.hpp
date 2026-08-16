// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Applies platform-independent input events to CNA's internal input state.
     *
     * Native event translation belongs to the selected platform implementation. This bridge is
     * deliberately downstream of that translation so the input state machine is shared by SDL3,
     * terminal and future platform implementations.
     */
    class PlatformInputBridge
    {
    public:
        /** @brief Processes one event, ignoring alternatives unrelated to input state. */
        static void ProcessEvent(const CNA::Platform::PlatformEvent& event);
    };
}
