// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Describes the capabilities of a gamepad controller.
     *
     * FNA declares every property here `{ get; internal set; }`; CNA maps the internal
     * setter to a `CNAEXT`-tagged public setter (only `CNA::Internal::Input::SdlInputBridge`
     * populates a connected instance in practice). The 10 EXT properties are FNA extensions,
     * so both their getters and setters are `CNAEXT`.
     */
    struct GamePadCapabilities
    {
        /** @brief Constructs disconnected capabilities with every flag false. */
        GamePadCapabilities() = default;

        /** @brief Indicates whether the controller is connected. */
        [[nodiscard]] bool getIsConnectedProperty() const;
        /** @brief Sets whether the controller is connected. */
        CNAEXT void setIsConnectedProperty(bool value);

        /** @brief Indicates whether the controller has an A button. */
        [[nodiscard]] bool getHasAButtonProperty() const;
        /** @brief Sets whether the controller has an A button. */
        CNAEXT void setHasAButtonProperty(bool value);

        /** @brief Indicates whether the controller has a Back button. */
        [[nodiscard]] bool getHasBackButtonProperty() const;
        /** @brief Sets whether the controller has a Back button. */
        CNAEXT void setHasBackButtonProperty(bool value);

        /** @brief Indicates whether the controller has a B button. */
        [[nodiscard]] bool getHasBButtonProperty() const;
        /** @brief Sets whether the controller has a B button. */
        CNAEXT void setHasBButtonProperty(bool value);

        /** @brief Indicates whether the controller has a DPad Down button. */
        [[nodiscard]] bool getHasDPadDownButtonProperty() const;
        /** @brief Sets whether the controller has a DPad Down button. */
        CNAEXT void setHasDPadDownButtonProperty(bool value);

        /** @brief Indicates whether the controller has a DPad Left button. */
        [[nodiscard]] bool getHasDPadLeftButtonProperty() const;
        /** @brief Sets whether the controller has a DPad Left button. */
        CNAEXT void setHasDPadLeftButtonProperty(bool value);

        /** @brief Indicates whether the controller has a DPad Right button. */
        [[nodiscard]] bool getHasDPadRightButtonProperty() const;
        /** @brief Sets whether the controller has a DPad Right button. */
        CNAEXT void setHasDPadRightButtonProperty(bool value);

        /** @brief Indicates whether the controller has a DPad Up button. */
        [[nodiscard]] bool getHasDPadUpButtonProperty() const;
        /** @brief Sets whether the controller has a DPad Up button. */
        CNAEXT void setHasDPadUpButtonProperty(bool value);

        /** @brief Indicates whether the controller has a left shoulder button. */
        [[nodiscard]] bool getHasLeftShoulderButtonProperty() const;
        /** @brief Sets whether the controller has a left shoulder button. */
        CNAEXT void setHasLeftShoulderButtonProperty(bool value);

        /** @brief Indicates whether the controller has a left stick button. */
        [[nodiscard]] bool getHasLeftStickButtonProperty() const;
        /** @brief Sets whether the controller has a left stick button. */
        CNAEXT void setHasLeftStickButtonProperty(bool value);

        /** @brief Indicates whether the controller has a right shoulder button. */
        [[nodiscard]] bool getHasRightShoulderButtonProperty() const;
        /** @brief Sets whether the controller has a right shoulder button. */
        CNAEXT void setHasRightShoulderButtonProperty(bool value);

        /** @brief Indicates whether the controller has a right stick button. */
        [[nodiscard]] bool getHasRightStickButtonProperty() const;
        /** @brief Sets whether the controller has a right stick button. */
        CNAEXT void setHasRightStickButtonProperty(bool value);

        /** @brief Indicates whether the controller has a Start button. */
        [[nodiscard]] bool getHasStartButtonProperty() const;
        /** @brief Sets whether the controller has a Start button. */
        CNAEXT void setHasStartButtonProperty(bool value);

        /** @brief Indicates whether the controller has an X button. */
        [[nodiscard]] bool getHasXButtonProperty() const;
        /** @brief Sets whether the controller has an X button. */
        CNAEXT void setHasXButtonProperty(bool value);

        /** @brief Indicates whether the controller has a Y button. */
        [[nodiscard]] bool getHasYButtonProperty() const;
        /** @brief Sets whether the controller has a Y button. */
        CNAEXT void setHasYButtonProperty(bool value);

        /** @brief Indicates whether the controller has a big button. */
        [[nodiscard]] bool getHasBigButtonProperty() const;
        /** @brief Sets whether the controller has a big button. */
        CNAEXT void setHasBigButtonProperty(bool value);

        /** @brief Indicates whether the controller has a left X thumb stick axis. */
        [[nodiscard]] bool getHasLeftXThumbStickProperty() const;
        /** @brief Sets whether the controller has a left X thumb stick axis. */
        CNAEXT void setHasLeftXThumbStickProperty(bool value);

        /** @brief Indicates whether the controller has a left Y thumb stick axis. */
        [[nodiscard]] bool getHasLeftYThumbStickProperty() const;
        /** @brief Sets whether the controller has a left Y thumb stick axis. */
        CNAEXT void setHasLeftYThumbStickProperty(bool value);

        /** @brief Indicates whether the controller has a right X thumb stick axis. */
        [[nodiscard]] bool getHasRightXThumbStickProperty() const;
        /** @brief Sets whether the controller has a right X thumb stick axis. */
        CNAEXT void setHasRightXThumbStickProperty(bool value);

        /** @brief Indicates whether the controller has a right Y thumb stick axis. */
        [[nodiscard]] bool getHasRightYThumbStickProperty() const;
        /** @brief Sets whether the controller has a right Y thumb stick axis. */
        CNAEXT void setHasRightYThumbStickProperty(bool value);

        /** @brief Indicates whether the controller has a left trigger. */
        [[nodiscard]] bool getHasLeftTriggerProperty() const;
        /** @brief Sets whether the controller has a left trigger. */
        CNAEXT void setHasLeftTriggerProperty(bool value);

        /** @brief Indicates whether the controller has a right trigger. */
        [[nodiscard]] bool getHasRightTriggerProperty() const;
        /** @brief Sets whether the controller has a right trigger. */
        CNAEXT void setHasRightTriggerProperty(bool value);

        /** @brief Indicates whether the controller has a left vibration motor. */
        [[nodiscard]] bool getHasLeftVibrationMotorProperty() const;
        /** @brief Sets whether the controller has a left vibration motor. */
        CNAEXT void setHasLeftVibrationMotorProperty(bool value);

        /** @brief Indicates whether the controller has a right vibration motor. */
        [[nodiscard]] bool getHasRightVibrationMotorProperty() const;
        /** @brief Sets whether the controller has a right vibration motor. */
        CNAEXT void setHasRightVibrationMotorProperty(bool value);

        /** @brief Indicates whether the controller supports voice. */
        [[nodiscard]] bool getHasVoiceSupportProperty() const;
        /** @brief Sets whether the controller supports voice. */
        CNAEXT void setHasVoiceSupportProperty(bool value);

        /** @brief Gets the type of this gamepad. */
        [[nodiscard]] GamePadType getGamePadTypeProperty() const;
        /** @brief Sets the type of this gamepad. */
        CNAEXT void setGamePadTypeProperty(GamePadType value);

        // FNA extensions — both getter and setter are CNAEXT (not part of the XNA 4.0 API).

        /** @brief Indicates whether the controller has a light bar (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasLightBarEXTProperty() const;
        /** @brief Sets whether the controller has a light bar (FNA extension). */
        CNAEXT void setHasLightBarEXTProperty(bool value);

        /** @brief Indicates whether the controller has trigger vibration motors (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasTriggerVibrationMotorsEXTProperty() const;
        /** @brief Sets whether the controller has trigger vibration motors (FNA extension). */
        CNAEXT void setHasTriggerVibrationMotorsEXTProperty(bool value);

        /** @brief Indicates whether the controller has a Misc1 button (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasMisc1EXTProperty() const;
        /** @brief Sets whether the controller has a Misc1 button (FNA extension). */
        CNAEXT void setHasMisc1EXTProperty(bool value);

        /** @brief Indicates whether the controller has a Paddle1 button (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasPaddle1EXTProperty() const;
        /** @brief Sets whether the controller has a Paddle1 button (FNA extension). */
        CNAEXT void setHasPaddle1EXTProperty(bool value);

        /** @brief Indicates whether the controller has a Paddle2 button (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasPaddle2EXTProperty() const;
        /** @brief Sets whether the controller has a Paddle2 button (FNA extension). */
        CNAEXT void setHasPaddle2EXTProperty(bool value);

        /** @brief Indicates whether the controller has a Paddle3 button (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasPaddle3EXTProperty() const;
        /** @brief Sets whether the controller has a Paddle3 button (FNA extension). */
        CNAEXT void setHasPaddle3EXTProperty(bool value);

        /** @brief Indicates whether the controller has a Paddle4 button (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasPaddle4EXTProperty() const;
        /** @brief Sets whether the controller has a Paddle4 button (FNA extension). */
        CNAEXT void setHasPaddle4EXTProperty(bool value);

        /** @brief Indicates whether the controller has a touch pad (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasTouchPadEXTProperty() const;
        /** @brief Sets whether the controller has a touch pad (FNA extension). */
        CNAEXT void setHasTouchPadEXTProperty(bool value);

        /** @brief Indicates whether the controller has a gyroscope (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasGyroEXTProperty() const;
        /** @brief Sets whether the controller has a gyroscope (FNA extension). */
        CNAEXT void setHasGyroEXTProperty(bool value);

        /** @brief Indicates whether the controller has an accelerometer (FNA extension). */
        CNAEXT [[nodiscard]] bool getHasAccelerometerEXTProperty() const;
        /** @brief Sets whether the controller has an accelerometer (FNA extension). */
        CNAEXT void setHasAccelerometerEXTProperty(bool value);

    private:
        bool isConnected_              = false;
        bool hasAButton_               = false;
        bool hasBackButton_            = false;
        bool hasBButton_               = false;
        bool hasDPadDownButton_        = false;
        bool hasDPadLeftButton_        = false;
        bool hasDPadRightButton_       = false;
        bool hasDPadUpButton_          = false;
        bool hasLeftShoulderButton_    = false;
        bool hasLeftStickButton_       = false;
        bool hasRightShoulderButton_   = false;
        bool hasRightStickButton_      = false;
        bool hasStartButton_           = false;
        bool hasXButton_               = false;
        bool hasYButton_               = false;
        bool hasBigButton_             = false;
        bool hasLeftXThumbStick_       = false;
        bool hasLeftYThumbStick_       = false;
        bool hasRightXThumbStick_      = false;
        bool hasRightYThumbStick_      = false;
        bool hasLeftTrigger_           = false;
        bool hasRightTrigger_          = false;
        bool hasLeftVibrationMotor_    = false;
        bool hasRightVibrationMotor_   = false;
        bool hasVoiceSupport_          = false;
        GamePadType gamePadType_       = GamePadType::Unknown;

        bool hasLightBarEXT_               = false;
        bool hasTriggerVibrationMotorsEXT_ = false;
        bool hasMisc1EXT_                  = false;
        bool hasPaddle1EXT_                = false;
        bool hasPaddle2EXT_                = false;
        bool hasPaddle3EXT_                = false;
        bool hasPaddle4EXT_                = false;
        bool hasTouchPadEXT_               = false;
        bool hasGyroEXT_                   = false;
        bool hasAccelerometerEXT_          = false;
    };
}
