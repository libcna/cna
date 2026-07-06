// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/GamePadButtonLabel.hpp"
#include "CNA/Input/PowerState.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Provides access to gamepad state snapshots.
     */
    class GamePad
    {
    public:
        GamePad() = delete;

        /**
         * @brief Returns the capabilities of the gamepad at the given player index.
         * @param playerIndex The player index to query.
         * @return The gamepad capabilities.
         */
        static GamePadCapabilities GetCapabilities(PlayerIndex playerIndex);

        /**
         * @brief Returns a snapshot of the current gamepad state using IndependentAxes dead zone.
         * @param playerIndex The player index to query.
         * @return The current gamepad state.
         */
        static GamePadState GetState(PlayerIndex playerIndex);

        /**
         * @brief Returns a snapshot of the current gamepad state using the specified dead zone mode.
         * @param playerIndex The player index to query.
         * @param deadZoneMode The dead zone processing mode to apply.
         * @return The current gamepad state.
         */
        static GamePadState GetState(PlayerIndex playerIndex, GamePadDeadZone deadZoneMode);

        /**
         * @brief Sets vibration motor levels. Returns false if vibration is not supported.
         * @param playerIndex The player index of the controller.
         * @param leftMotor The left motor speed in [0, 1].
         * @param rightMotor The right motor speed in [0, 1].
         * @return True if vibration was set; false if not supported.
         */
        static bool SetVibration(PlayerIndex playerIndex, float leftMotor, float rightMotor);

        /**
         * @brief Returns a GUID string identifying the physical gamepad (FNA extension).
         * @param playerIndex The player index of the controller.
         * @return The GUID string.
         */
        NOXNA static std::string GetGUIDEXT(PlayerIndex playerIndex);

        /**
         * @brief Sets the light bar color (PS4/PS5 controllers, FNA extension).
         * @param playerIndex The player index of the controller.
         * @param color The color to set on the light bar.
         */
        NOXNA static void SetLightBarEXT(PlayerIndex playerIndex,
                                         const Microsoft::Xna::Framework::Color& color);

        /**
         * @brief Sets trigger vibration motors. Returns false if not supported (FNA extension).
         * @param playerIndex The player index of the controller.
         * @param leftTrigger Left trigger motor speed in [0, 1].
         * @param rightTrigger Right trigger motor speed in [0, 1].
         * @return True if trigger vibration was set; false if not supported.
         */
        NOXNA static bool SetTriggerVibrationEXT(PlayerIndex playerIndex,
                                                  float leftTrigger, float rightTrigger);

        /**
         * @brief Reads gyroscope data. Returns false if not available (FNA extension).
         * @param playerIndex The player index of the controller.
         * @param gyro Output parameter receiving the gyroscope vector.
         * @return True if data is available; false otherwise.
         */
        NOXNA static bool GetGyroEXT(PlayerIndex playerIndex,
                                      Microsoft::Xna::Framework::Vector3& gyro);

        /**
         * @brief Reads accelerometer data. Returns false if not available (FNA extension).
         * @param playerIndex The player index of the controller.
         * @param accel Output parameter receiving the accelerometer vector.
         * @return True if data is available; false otherwise.
         */
        NOXNA static bool GetAccelerometerEXT(PlayerIndex playerIndex,
                                               Microsoft::Xna::Framework::Vector3& accel);

        /**
         * @brief NOXNA/EXT: gets the controller's SDL player index (the 0-based player-number LED).
         * @param playerIndex The player index (slot) of the controller.
         * @return The device player index, or -1 if the controller is disconnected or the index is unset.
         */
        NOXNA static int GetPlayerIndexEXT(PlayerIndex playerIndex);

        /**
         * @brief NOXNA/EXT: sets the controller's SDL player index (the 0-based player-number LED).
         * @param playerIndex The player index (slot) of the controller.
         * @param index The 0-based player-number LED to assign.
         * @return True on success; false if the controller is disconnected or SDL rejected the change.
         */
        NOXNA static bool SetPlayerIndexEXT(PlayerIndex playerIndex, int index);

        /**
         * @brief NOXNA/EXT: reads the controller's battery/charge state.
         * @param playerIndex The player index (slot) of the controller.
         * @param percent Output receiving the battery charge (0-100), or -1 if unknown/disconnected.
         * @return The power state, or PowerStateEXT::Error if the controller is disconnected.
         */
        NOXNA static CNA::Input::PowerStateEXT GetPowerInfoEXT(PlayerIndex playerIndex, int& percent);

        /**
         * @brief NOXNA/EXT: returns the printed glyph label for a face button on this controller.
         * @param playerIndex The player index (slot) of the controller.
         * @param button The XNA button to query (only physical buttons have a label).
         * @return The button's label, or GamePadButtonLabelEXT::Unknown if disconnected or unlabeled.
         */
        NOXNA static CNA::Input::GamePadButtonLabelEXT GetButtonLabelEXT(PlayerIndex playerIndex, Buttons button);

        /** @brief Left stick dead zone threshold (XInput-based). */
        NOXNA static constexpr float LeftDeadZone     = 7849.0f / 32768.0f;
        /** @brief Right stick dead zone threshold (XInput-based). */
        NOXNA static constexpr float RightDeadZone    = 8689.0f / 32768.0f;
        /** @brief Trigger pressed threshold (XInput-based). */
        NOXNA static constexpr float TriggerThreshold = 30.0f / 255.0f;

        /**
         * @brief Applies dead zone exclusion to a single axis value.
         * @param value The raw axis value.
         * @param deadZone The dead zone threshold.
         * @return The processed axis value.
         */
        NOXNA static float ExcludeAxisDeadZone(float value, float deadZone);
    };
}
