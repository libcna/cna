#pragma once

#include <SDL3/SDL.h>
#include <string>

#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Bridge between SDL3 events and CNA internal input state.
     *
     * This bridge knows SDL types, but exposes them only internally.
     *
     * @note Status: PARTIAL
     */
    class SdlInputBridge
    {
    public:
        /**
         * @brief Processes one SDL event and propagates relevant changes to InputManager.
         */
        static void ProcessEvent(const SDL_Event& event);

        /**
         * @brief Triggers rumble on the gamepad for the given player.
         * @return true if vibration was successfully set.
         */
        static bool SetVibration(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            float leftMotor,
            float rightMotor
        );

        /**
         * @brief Triggers trigger rumble on the gamepad for the given player.
         * @return true if trigger vibration was successfully set.
         */
        static bool SetTriggerVibration(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            float leftTrigger,
            float rightTrigger
        );

        /**
         * @brief Returns the SDL GUID string for the gamepad at the given player slot.
         */
        static std::string GetGUID(Microsoft::Xna::Framework::PlayerIndex playerIndex);
    };
}
