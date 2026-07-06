// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Internal/Input/SdlGamepadBackend.hpp"

// Test-only fake implementation of the internal SDL gamepad seam. Lets gamepad runtime behavior
// (hot-plug, slot assignment, capabilities, rumble, sensors, GUID) be exercised with NO real
// hardware. Opaque SDL_Gamepad*/SDL_Joystick* handles are just reinterpret-cast FakeDevice pointers
// that are only ever handed back to this fake. NOT compiled into production.
namespace CNA::Internal::Input::test_support
{
    // One touchpad finger's contact state, as reported by SDL_GetGamepadTouchpadFinger.
    struct FakeTouchpadFinger
    {
        bool down = false;
        float x = 0.0f;
        float y = 0.0f;
        float pressure = 0.0f;
    };

    // Per-device fake description; register one per synthetic joystick instance id before delivering
    // an SDL_EVENT_GAMEPAD_ADDED for that id.
    struct FakeGamepadConfig
    {
        bool isGamepad = true;
        std::set<SDL_GamepadButton> buttons;
        std::set<SDL_GamepadAxis> axes;
        std::set<SDL_SensorType> sensors;
        int numTouchpads = 0;
        std::vector<std::vector<FakeTouchpadFinger>> touchpadFingers; // [touchpad] -> its fingers
        bool rumble = false;
        bool triggerRumble = false;
        bool rgbLed = false;
        Uint16 vendor = 0;
        Uint16 product = 0;
        SDL_GamepadType gamepadType = SDL_GAMEPAD_TYPE_STANDARD;
        SDL_JoystickType joystickType = SDL_JOYSTICK_TYPE_GAMEPAD;
        std::array<float, 3> gyroData{{0.0f, 0.0f, 0.0f}};
        std::array<float, 3> accelData{{0.0f, 0.0f, 0.0f}};
        bool sensorReadFails = false;
        int playerIndex = -1; // SDL device player index (LED number); -1 = unset
        SDL_PowerState powerState = SDL_POWERSTATE_UNKNOWN; // battery/charge state reported by the fake
        int powerPercent = -1; // battery charge 0-100, or -1 if unknown
        std::map<SDL_GamepadButton, SDL_GamepadButtonLabel> buttonLabels; // printed glyph per face button
        std::string name;                // SDL_GetGamepadName
        std::string path;                // SDL_GetGamepadPath
        std::string serial;              // SDL_GetGamepadSerial
        Uint16 firmwareVersion = 0;      // SDL_GetGamepadFirmwareVersion
        Uint64 steamHandle = 0;          // SDL_GetGamepadSteamHandle
        SDL_JoystickConnectionState connectionState = SDL_JOYSTICK_CONNECTION_UNKNOWN; // wired/wireless
    };

    class FakeSdlGamepadBackend final : public ISdlGamepadBackend
    {
    public:
        // --- test setup / introspection ---
        void Register(SDL_JoystickID id, FakeGamepadConfig cfg) { registered_[id] = std::move(cfg); }

        int openCount = 0;               // total OpenGamepad calls
        int closeCount = 0;              // total CloseGamepad calls
        int rumbleCalls = 0;             // total RumbleGamepad calls (used to prove GetCapabilities doesn't rumble)
        int triggerRumbleCalls = 0;
        int ledCalls = 0;
        int setSensorEnabledCalls = 0;   // total SetGamepadSensorEnabled calls (proves lazy enable-once)
        Uint16 lastRumbleLow = 0, lastRumbleHigh = 0;
        Uint16 lastTriggerLow = 0, lastTriggerHigh = 0;
        Uint8 lastLedR = 0, lastLedG = 0, lastLedB = 0;
        int setPlayerIndexCalls = 0;     // total SetGamepadPlayerIndex calls
        int lastSetPlayerIndex = -1;     // arg of the last SetGamepadPlayerIndex

        ~FakeSdlGamepadBackend() override
        {
            for (auto& d : devices_)
                if (d->props != 0)
                    SDL_DestroyProperties(d->props);
        }

        // --- ISdlGamepadBackend ---
        bool IsGamepad(SDL_JoystickID id) override
        {
            const auto it = registered_.find(id);
            return it != registered_.end() && it->second.isGamepad;
        }

        SDL_Gamepad* OpenGamepad(SDL_JoystickID id) override
        {
            const auto it = registered_.find(id);
            if (it == registered_.end() || !it->second.isGamepad)
                return nullptr;
            auto dev = std::make_unique<FakeDevice>();
            dev->id = id;
            dev->cfg = it->second;
            ++openCount;
            FakeDevice* raw = dev.get();
            devices_.push_back(std::move(dev));
            return reinterpret_cast<SDL_Gamepad*>(raw);
        }

        void CloseGamepad(SDL_Gamepad* gamepad) override
        {
            if (FakeDevice* d = dev(gamepad))
            {
                d->closed = true;
                ++closeCount;
                lastClosedId = d->id;
                if (d->props != 0)
                {
                    SDL_DestroyProperties(d->props);
                    d->props = 0;
                }
            }
        }

        SDL_Joystick* GetGamepadJoystick(SDL_Gamepad* gamepad) override
        {
            return reinterpret_cast<SDL_Joystick*>(dev(gamepad));
        }

        SDL_JoystickType GetJoystickType(SDL_Joystick* joystick) override
        {
            FakeDevice* d = devFromJoystick(joystick);
            return d ? d->cfg.joystickType : SDL_JOYSTICK_TYPE_UNKNOWN;
        }

        bool GamepadHasButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) override
        {
            FakeDevice* d = dev(gamepad);
            return d != nullptr && d->cfg.buttons.count(button) > 0;
        }

        bool GamepadHasAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) override
        {
            FakeDevice* d = dev(gamepad);
            return d != nullptr && d->cfg.axes.count(axis) > 0;
        }

        int GetNumGamepadTouchpads(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.numTouchpads : 0;
        }

        int GetNumGamepadTouchpadFingers(SDL_Gamepad* gamepad, int touchpad) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr || touchpad < 0 ||
                touchpad >= static_cast<int>(d->cfg.touchpadFingers.size()))
                return 0;
            return static_cast<int>(d->cfg.touchpadFingers[touchpad].size());
        }

        bool GetGamepadTouchpadFinger(SDL_Gamepad* gamepad, int touchpad, int finger,
                                      bool* down, float* x, float* y, float* pressure) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr || touchpad < 0 ||
                touchpad >= static_cast<int>(d->cfg.touchpadFingers.size()))
                return false;
            const auto& fingers = d->cfg.touchpadFingers[touchpad];
            if (finger < 0 || finger >= static_cast<int>(fingers.size()))
                return false;
            const FakeTouchpadFinger& f = fingers[finger];
            if (down != nullptr)     *down = f.down;
            if (x != nullptr)        *x = f.x;
            if (y != nullptr)        *y = f.y;
            if (pressure != nullptr) *pressure = f.pressure;
            return true;
        }

        bool GamepadHasSensor(SDL_Gamepad* gamepad, SDL_SensorType type) override
        {
            FakeDevice* d = dev(gamepad);
            return d != nullptr && d->cfg.sensors.count(type) > 0;
        }

        SDL_PropertiesID GetGamepadProperties(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return 0;
            if (d->props == 0)
            {
                d->props = SDL_CreateProperties();
                SDL_SetBooleanProperty(d->props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, d->cfg.rumble);
                SDL_SetBooleanProperty(d->props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, d->cfg.triggerRumble);
                SDL_SetBooleanProperty(d->props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, d->cfg.rgbLed);
            }
            return d->props;
        }

        bool RumbleGamepad(SDL_Gamepad* gamepad, Uint16 lowFreq, Uint16 highFreq, Uint32) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return false;
            ++rumbleCalls;
            lastRumbleLow = lowFreq;
            lastRumbleHigh = highFreq;
            return d->cfg.rumble;
        }

        bool RumbleGamepadTriggers(SDL_Gamepad* gamepad, Uint16 leftFreq, Uint16 rightFreq, Uint32) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return false;
            ++triggerRumbleCalls;
            lastTriggerLow = leftFreq;
            lastTriggerHigh = rightFreq;
            return d->cfg.triggerRumble;
        }

        bool SetGamepadLED(SDL_Gamepad* gamepad, Uint8 red, Uint8 green, Uint8 blue) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return false;
            ++ledCalls;
            lastLedR = red;
            lastLedG = green;
            lastLedB = blue;
            return d->cfg.rgbLed;
        }

        bool GamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type) override
        {
            FakeDevice* d = dev(gamepad);
            return d != nullptr && d->enabledSensors.count(type) > 0;
        }

        bool SetGamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type, bool enabled) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return false;
            ++setSensorEnabledCalls;
            if (enabled)
                d->enabledSensors.insert(type);
            else
                d->enabledSensors.erase(type);
            return true;
        }

        bool GetGamepadSensorData(SDL_Gamepad* gamepad, SDL_SensorType type, float* data, int count) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr || d->cfg.sensorReadFails || data == nullptr || count < 3)
                return false;
            const auto& src = (type == SDL_SENSOR_GYRO) ? d->cfg.gyroData : d->cfg.accelData;
            data[0] = src[0];
            data[1] = src[1];
            data[2] = src[2];
            return true;
        }

        Uint16 GetJoystickVendor(SDL_Joystick* joystick) override
        {
            FakeDevice* d = devFromJoystick(joystick);
            return d ? d->cfg.vendor : 0;
        }

        Uint16 GetJoystickProduct(SDL_Joystick* joystick) override
        {
            FakeDevice* d = devFromJoystick(joystick);
            return d ? d->cfg.product : 0;
        }

        SDL_GamepadType GetGamepadType(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.gamepadType : SDL_GAMEPAD_TYPE_UNKNOWN;
        }

        int GetGamepadPlayerIndex(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.playerIndex : -1;
        }

        bool SetGamepadPlayerIndex(SDL_Gamepad* gamepad, int playerIndex) override
        {
            ++setPlayerIndexCalls;
            lastSetPlayerIndex = playerIndex;
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return false;
            d->cfg.playerIndex = playerIndex; // per-device copy; a later Get reads it back
            return true;
        }

        SDL_PowerState GetGamepadPowerInfo(SDL_Gamepad* gamepad, int* percent) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
            {
                if (percent != nullptr)
                    *percent = -1;
                return SDL_POWERSTATE_ERROR;
            }
            if (percent != nullptr)
                *percent = d->cfg.powerPercent;
            return d->cfg.powerState;
        }

        SDL_GamepadButtonLabel GetGamepadButtonLabel(SDL_Gamepad* gamepad, SDL_GamepadButton button) override
        {
            FakeDevice* d = dev(gamepad);
            if (d == nullptr)
                return SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
            const auto it = d->cfg.buttonLabels.find(button);
            return it != d->cfg.buttonLabels.end() ? it->second : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
        }

        std::string GetGamepadName(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.name : "";
        }
        std::string GetGamepadPath(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.path : "";
        }
        std::string GetGamepadSerial(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.serial : "";
        }
        Uint16 GetGamepadFirmwareVersion(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.firmwareVersion : 0;
        }
        Uint64 GetGamepadSteamHandle(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.steamHandle : 0;
        }
        SDL_JoystickConnectionState GetGamepadConnectionState(SDL_Gamepad* gamepad) override
        {
            FakeDevice* d = dev(gamepad);
            return d ? d->cfg.connectionState : SDL_JOYSTICK_CONNECTION_INVALID;
        }

        SDL_JoystickID lastClosedId = 0;

    private:
        struct FakeDevice
        {
            SDL_JoystickID id = 0;
            FakeGamepadConfig cfg;
            bool closed = false;
            SDL_PropertiesID props = 0;
            std::set<SDL_SensorType> enabledSensors;
        };

        FakeDevice* dev(SDL_Gamepad* g) { return reinterpret_cast<FakeDevice*>(g); }
        FakeDevice* devFromJoystick(SDL_Joystick* j) { return reinterpret_cast<FakeDevice*>(j); }

        std::map<SDL_JoystickID, FakeGamepadConfig> registered_;
        std::deque<std::unique_ptr<FakeDevice>> devices_;
    };

    // A fully-featured pad (all buttons/axes/sensors/rumble/LED) for the common case.
    inline FakeGamepadConfig FullyFeaturedGamepad()
    {
        FakeGamepadConfig cfg;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            cfg.buttons.insert(static_cast<SDL_GamepadButton>(b));
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            cfg.axes.insert(static_cast<SDL_GamepadAxis>(a));
        cfg.sensors = {SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL};
        cfg.numTouchpads = 1;
        cfg.rumble = true;
        cfg.triggerRumble = true;
        cfg.rgbLed = true;
        cfg.gamepadType = SDL_GAMEPAD_TYPE_XBOXONE;
        cfg.joystickType = SDL_JOYSTICK_TYPE_GAMEPAD;
        return cfg;
    }
}
