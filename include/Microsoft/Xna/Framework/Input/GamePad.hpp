#pragma once

#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Input
{
    /// Provides access to gamepad state snapshots.
    class GamePad
    {
    public:
        GamePad() = delete;

        /// Returns the capabilities of the gamepad at the given player index.
        static GamePadCapabilities GetCapabilities(PlayerIndex playerIndex);

        /// Returns a snapshot of the current gamepad state using IndependentAxes dead zone.
        static GamePadState GetState(PlayerIndex playerIndex);

        /// Returns a snapshot of the current gamepad state using the specified dead zone mode.
        static GamePadState GetState(PlayerIndex playerIndex, GamePadDeadZone deadZoneMode);

        /// Sets motor vibration. Returns false if not supported.
        static bool SetVibration(PlayerIndex playerIndex, float leftMotor, float rightMotor);

        // FNA Extensions
        /// Returns a GUID string identifying the physical gamepad.
        NOXNA static std::string GetGUIDEXT(PlayerIndex playerIndex);

        /// Sets the light bar color (PS4/PS5 controllers).
        NOXNA static void SetLightBarEXT(PlayerIndex playerIndex,
                                         const Microsoft::Xna::Framework::Color& color);

        /// Sets trigger vibration motors. Returns false if not supported.
        NOXNA static bool SetTriggerVibrationEXT(PlayerIndex playerIndex,
                                                  float leftTrigger, float rightTrigger);

        /// Reads gyroscope data. Returns false if not available.
        NOXNA static bool GetGyroEXT(PlayerIndex playerIndex,
                                      Microsoft::Xna::Framework::Vector3& gyro);

        /// Reads accelerometer data. Returns false if not available.
        NOXNA static bool GetAccelerometerEXT(PlayerIndex playerIndex,
                                               Microsoft::Xna::Framework::Vector3& accel);

        // Internal constants (XInput-based dead zone values)
        NOXNA static constexpr float LeftDeadZone     = 7849.0f / 32768.0f;
        NOXNA static constexpr float RightDeadZone    = 8689.0f / 32768.0f;
        NOXNA static constexpr float TriggerThreshold = 30.0f / 255.0f;

        /// Applies dead zone exclusion to a single axis value.
        NOXNA static float ExcludeAxisDeadZone(float value, float deadZone);
    };
}
