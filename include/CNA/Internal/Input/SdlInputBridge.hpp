#pragma once

#include <SDL3/SDL.h>

namespace CNA::Internal::Input {
    /**
     * @brief Bridge between SDL3 events and CNA internal input state.
     *
     * This bridge knows SDL types, but exposes them only internally.
     *
     * Currently supports Mouse, basic Keyboard and basic TouchPanel state propagation.
     *
     * @note Status: PARTIAL
     */
    class SdlInputBridge {
    public:
        /**
         * @brief Processes one SDL event and propagates relevant changes to InputManager.
         */
        static void ProcessEvent(const SDL_Event& event);
    };
}
