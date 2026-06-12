// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /// Describes the capabilities of a gamepad controller.
    struct GamePadCapabilities
    {
        bool IsConnected              = false;
        bool HasAButton               = false;
        bool HasBackButton            = false;
        bool HasBButton               = false;
        bool HasDPadDownButton        = false;
        bool HasDPadLeftButton        = false;
        bool HasDPadRightButton       = false;
        bool HasDPadUpButton          = false;
        bool HasLeftShoulderButton    = false;
        bool HasLeftStickButton       = false;
        bool HasRightShoulderButton   = false;
        bool HasRightStickButton      = false;
        bool HasStartButton           = false;
        bool HasXButton               = false;
        bool HasYButton               = false;
        bool HasBigButton             = false;
        bool HasLeftXThumbStick       = false;
        bool HasLeftYThumbStick       = false;
        bool HasRightXThumbStick      = false;
        bool HasRightYThumbStick      = false;
        bool HasLeftTrigger           = false;
        bool HasRightTrigger          = false;
        bool HasLeftVibrationMotor    = false;
        bool HasRightVibrationMotor   = false;
        bool HasVoiceSupport          = false;
        GamePadType GamePadType_      = GamePadType::Unknown;

        // FNA extensions
        bool HasLightBarEXT                = false;
        bool HasTriggerVibrationMotorsEXT  = false;
        bool HasMisc1EXT                   = false;
        bool HasPaddle1EXT                 = false;
        bool HasPaddle2EXT                 = false;
        bool HasPaddle3EXT                 = false;
        bool HasPaddle4EXT                 = false;
        bool HasTouchPadEXT                = false;
        bool HasGyroEXT                    = false;
        bool HasAccelerometerEXT           = false;

        /// Gets the type of gamepad.
        [[nodiscard]] GamePadType getGamePadTypeProperty() const { return GamePadType_; }
    };
}
