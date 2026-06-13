// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Describes the capabilities of a gamepad controller.
     */
    struct GamePadCapabilities
    {
        /** @brief Indicates whether the controller is connected. */
        bool IsConnected              = false;
        /** @brief Indicates whether the controller has an A button. */
        bool HasAButton               = false;
        /** @brief Indicates whether the controller has a Back button. */
        bool HasBackButton            = false;
        /** @brief Indicates whether the controller has a B button. */
        bool HasBButton               = false;
        /** @brief Indicates whether the controller has a DPad Down button. */
        bool HasDPadDownButton        = false;
        /** @brief Indicates whether the controller has a DPad Left button. */
        bool HasDPadLeftButton        = false;
        /** @brief Indicates whether the controller has a DPad Right button. */
        bool HasDPadRightButton       = false;
        /** @brief Indicates whether the controller has a DPad Up button. */
        bool HasDPadUpButton          = false;
        /** @brief Indicates whether the controller has a left shoulder button. */
        bool HasLeftShoulderButton    = false;
        /** @brief Indicates whether the controller has a left stick button. */
        bool HasLeftStickButton       = false;
        /** @brief Indicates whether the controller has a right shoulder button. */
        bool HasRightShoulderButton   = false;
        /** @brief Indicates whether the controller has a right stick button. */
        bool HasRightStickButton      = false;
        /** @brief Indicates whether the controller has a Start button. */
        bool HasStartButton           = false;
        /** @brief Indicates whether the controller has an X button. */
        bool HasXButton               = false;
        /** @brief Indicates whether the controller has a Y button. */
        bool HasYButton               = false;
        /** @brief Indicates whether the controller has a big button. */
        bool HasBigButton             = false;
        /** @brief Indicates whether the controller has a left X thumb stick axis. */
        bool HasLeftXThumbStick       = false;
        /** @brief Indicates whether the controller has a left Y thumb stick axis. */
        bool HasLeftYThumbStick       = false;
        /** @brief Indicates whether the controller has a right X thumb stick axis. */
        bool HasRightXThumbStick      = false;
        /** @brief Indicates whether the controller has a right Y thumb stick axis. */
        bool HasRightYThumbStick      = false;
        /** @brief Indicates whether the controller has a left trigger. */
        bool HasLeftTrigger           = false;
        /** @brief Indicates whether the controller has a right trigger. */
        bool HasRightTrigger          = false;
        /** @brief Indicates whether the controller has a left vibration motor. */
        bool HasLeftVibrationMotor    = false;
        /** @brief Indicates whether the controller has a right vibration motor. */
        bool HasRightVibrationMotor   = false;
        /** @brief Indicates whether the controller supports voice. */
        bool HasVoiceSupport          = false;
        /** @brief The type of this gamepad. */
        GamePadType GamePadType_      = GamePadType::Unknown;

        // FNA extensions
        /** @brief Indicates whether the controller has a light bar (FNA extension). */
        bool HasLightBarEXT                = false;
        /** @brief Indicates whether the controller has trigger vibration motors (FNA extension). */
        bool HasTriggerVibrationMotorsEXT  = false;
        /** @brief Indicates whether the controller has a Misc1 button (FNA extension). */
        bool HasMisc1EXT                   = false;
        /** @brief Indicates whether the controller has a Paddle1 button (FNA extension). */
        bool HasPaddle1EXT                 = false;
        /** @brief Indicates whether the controller has a Paddle2 button (FNA extension). */
        bool HasPaddle2EXT                 = false;
        /** @brief Indicates whether the controller has a Paddle3 button (FNA extension). */
        bool HasPaddle3EXT                 = false;
        /** @brief Indicates whether the controller has a Paddle4 button (FNA extension). */
        bool HasPaddle4EXT                 = false;
        /** @brief Indicates whether the controller has a touch pad (FNA extension). */
        bool HasTouchPadEXT                = false;
        /** @brief Indicates whether the controller has a gyroscope (FNA extension). */
        bool HasGyroEXT                    = false;
        /** @brief Indicates whether the controller has an accelerometer (FNA extension). */
        bool HasAccelerometerEXT           = false;

        /**
         * @brief Gets the type of this gamepad.
         * @return The GamePadType value.
         */
        [[nodiscard]] GamePadType getGamePadTypeProperty() const { return GamePadType_; }
    };
}
