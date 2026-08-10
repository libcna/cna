// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <string>

// Forward-declared rather than including their CNA/Input/*.hpp headers: this is a strict-XNA
// header (P1-027/P1-028) and must not require a consumer to pull in a CNA::Input extension header
// just to use GamePad's non-EXT surface. GetPowerInfoEXT/GetButtonLabelEXT/GetConnectionStateEXT
// below only need the type names to declare their return types; GamePad.cpp already gets the real
// definitions transitively via CNA/Internal/Input/SdlInputBridge.hpp.
namespace CNA::Input
{
    enum class GamePadButtonLabelEXT;
    enum class GamePadConnectionStateEXT;
    enum class PowerStateEXT;
}

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
        CNAEXT static std::string GetGUIDEXT(PlayerIndex playerIndex);

        /**
         * @brief Sets the light bar color (PS4/PS5 controllers, FNA extension).
         * @param playerIndex The player index of the controller.
         * @param color The color to set on the light bar.
         */
        CNAEXT static void SetLightBarEXT(PlayerIndex playerIndex,
                                         const Microsoft::Xna::Framework::Color& color);

        /**
         * @brief Sets trigger vibration motors. Returns false if not supported (FNA extension).
         * @param playerIndex The player index of the controller.
         * @param leftTrigger Left trigger motor speed in [0, 1].
         * @param rightTrigger Right trigger motor speed in [0, 1].
         * @return True if trigger vibration was set; false if not supported.
         */
        CNAEXT static bool SetTriggerVibrationEXT(PlayerIndex playerIndex,
                                                  float leftTrigger, float rightTrigger);

        /**
         * @brief Reads gyroscope data. Returns false if not available (FNA extension).
         * @param playerIndex The player index of the controller.
         * @param gyro Output parameter receiving the gyroscope vector.
         * @return True if data is available; false otherwise.
         */
        CNAEXT static bool GetGyroEXT(PlayerIndex playerIndex,
                                      Microsoft::Xna::Framework::Vector3& gyro);

        /**
         * @brief Reads accelerometer data. Returns false if not available (FNA extension).
         * @param playerIndex The player index of the controller.
         * @param accel Output parameter receiving the accelerometer vector.
         * @return True if data is available; false otherwise.
         */
        CNAEXT static bool GetAccelerometerEXT(PlayerIndex playerIndex,
                                               Microsoft::Xna::Framework::Vector3& accel);

        /**
         * @brief CNAEXT/EXT: gets the controller's SDL player index (the 0-based player-number LED).
         * @param playerIndex The player index (slot) of the controller.
         * @return The device player index, or -1 if the controller is disconnected or the index is unset.
         */
        CNAEXT static int GetPlayerIndexEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: sets the controller's SDL player index (the 0-based player-number LED).
         * @param playerIndex The player index (slot) of the controller.
         * @param index The 0-based player-number LED to assign.
         * @return True on success; false if the controller is disconnected or SDL rejected the change.
         */
        CNAEXT static bool SetPlayerIndexEXT(PlayerIndex playerIndex, int index);

        /**
         * @brief CNAEXT/EXT: reads the controller's battery/charge state.
         * @param playerIndex The player index (slot) of the controller.
         * @param percent Output receiving the battery charge (0-100), or -1 if unknown/disconnected.
         * @return The power state, or PowerStateEXT::Error if the controller is disconnected.
         */
        CNAEXT static CNA::Input::PowerStateEXT GetPowerInfoEXT(PlayerIndex playerIndex, int& percent);

        /**
         * @brief CNAEXT/EXT: returns the printed glyph label for a face button on this controller.
         * @param playerIndex The player index (slot) of the controller.
         * @param button The XNA button to query (only physical buttons have a label).
         * @return The button's label, or GamePadButtonLabelEXT::Unknown if disconnected or unlabeled.
         */
        CNAEXT static CNA::Input::GamePadButtonLabelEXT GetButtonLabelEXT(PlayerIndex playerIndex, Buttons button);

        /**
         * @brief CNAEXT/EXT: the controller's human-readable name.
         * @param playerIndex The player index (slot) of the controller.
         * @return The name, or an empty string if disconnected or unknown.
         */
        CNAEXT static std::string GetNameEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: the controller's OS device path.
         * @param playerIndex The player index (slot) of the controller.
         * @return The device path, or an empty string if disconnected or unknown.
         */
        CNAEXT static std::string GetPathEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: the controller's hardware serial number.
         * @param playerIndex The player index (slot) of the controller.
         * @return The serial number, or an empty string if disconnected or unavailable.
         */
        CNAEXT static std::string GetSerialEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: the controller's firmware version.
         * @param playerIndex The player index (slot) of the controller.
         * @return The firmware version, or 0 if disconnected or unavailable.
         */
        CNAEXT static std::uint16_t GetFirmwareVersionEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: the controller's Steam Input handle.
         * @param playerIndex The player index (slot) of the controller.
         * @return The Steam Input handle, or 0 if disconnected or not a Steam virtual controller.
         */
        CNAEXT static std::uint64_t GetSteamHandleEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: how the controller is physically attached (wired vs. wireless).
         * @param playerIndex The player index (slot) of the controller.
         * @return The connection state, or GamePadConnectionStateEXT::Unknown if disconnected/unknown.
         */
        CNAEXT static CNA::Input::GamePadConnectionStateEXT GetConnectionStateEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: returns the number of touchpads on the controller.
         * @param playerIndex The player index (slot) of the controller.
         * @return The touchpad count, or 0 if disconnected or the controller has none.
         */
        CNAEXT static int GetTouchpadCountEXT(PlayerIndex playerIndex);

        /**
         * @brief CNAEXT/EXT: returns how many fingers a touchpad can report at once.
         * @param playerIndex The player index (slot) of the controller.
         * @param touchpad The touchpad index.
         * @return The finger capacity, or 0 if disconnected or the index is out of range.
         */
        CNAEXT static int GetTouchpadFingerCountEXT(PlayerIndex playerIndex, int touchpad);

        /**
         * @brief CNAEXT/EXT: reads one touchpad finger's contact state and position.
         * @param playerIndex The player index (slot) of the controller.
         * @param touchpad The touchpad index.
         * @param finger The finger index.
         * @param down Output receiving whether the finger is touching.
         * @param x Output receiving the normalized x position (0..1).
         * @param y Output receiving the normalized y position (0..1).
         * @param pressure Output receiving the finger pressure (0..1).
         * @return True if the finger data was read; false if disconnected or out of range.
         */
        CNAEXT static bool GetTouchpadFingerEXT(PlayerIndex playerIndex, int touchpad, int finger,
                                               bool& down, float& x, float& y, float& pressure);

        /** @brief Left stick dead zone threshold (XInput-based). */
        CNAEXT static constexpr float LeftDeadZone     = 7849.0f / 32768.0f;
        /** @brief Right stick dead zone threshold (XInput-based). */
        CNAEXT static constexpr float RightDeadZone    = 8689.0f / 32768.0f;
        /** @brief Trigger pressed threshold (XInput-based). */
        CNAEXT static constexpr float TriggerThreshold = 30.0f / 255.0f;

        /**
         * @brief Applies dead zone exclusion to a single axis value.
         * @param value The raw axis value.
         * @param deadZone The dead zone threshold.
         * @return The processed axis value.
         */
        CNAEXT static float ExcludeAxisDeadZone(float value, float deadZone);
    };
}
