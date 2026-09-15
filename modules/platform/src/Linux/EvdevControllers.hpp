// SPDX-License-Identifier: MS-PL
#pragma once

#include "EvdevDevice.hpp"
#include "EvdevLayout.hpp"

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace CNA::Platform::Linux {

    /**
     * @brief Every controller on the system, kept in step with `/dev/input`.
     *
     * One inotify watch on the input directory is the whole of hot-plug: a new node appears
     * (IN_CREATE), udev then grants the session its permissions (IN_ATTRIB), and an unplugged
     * device's node goes away (IN_DELETE) -- or its reads fail first. No libudev: the kernel's
     * own interfaces are all this needs, which keeps the platform free of a third-party library.
     *
     * Everything runs on the thread that pumps it; nothing here starts a thread of its own.
     */
    class EvdevControllerHub
    {
    public:
        /** @brief One controller the hub has open. */
        struct Controller
        {
            /** @brief The id shared by this controller's events and joystick view (never 0). */
            DeviceId id = 0;
            /** @brief The opened node. */
            std::unique_ptr<EvdevDevice> device;
            /** @brief Gamepad or joystick. */
            EvdevDeviceClass kind = EvdevDeviceClass::Joystick;
            /** @brief The XNA player slot, or -1 for a joystick or when all four are taken. */
            int slot = -1;
            /** @brief The mapped state, for a gamepad. */
            std::unique_ptr<EvdevGamepadState> gamepad;
            /** @brief Raw axes' codes, in code order, hats excluded. */
            std::vector<std::uint16_t> axisCodes;
            /** @brief Raw axes, scaled to [-32768, 32767], parallel to `axisCodes`. */
            std::vector<std::int16_t> axes;
            /** @brief Raw buttons' codes, in device order. */
            std::vector<std::uint16_t> buttonCodes;
            /** @brief Raw buttons, parallel to `buttonCodes`. */
            std::vector<bool> buttons;
            /** @brief Hat axes (HAT0X, HAT0Y, ... HAT3Y) the device has, as -1, 0 or 1. */
            std::array<int, 8> hatValues{};
            /** @brief How many hats (X/Y pairs) the device has. */
            int hatCount = 0;
        };

        /**
         * @brief Creates a hub for an input directory.
         *
         * @param directory The directory holding the event nodes; overridable for tests.
         * @param sysfsRoot Where sysfs describes those nodes without their being opened; a node
         * sysfs does not describe is opened and asked instead. Overridable for tests.
         */
        explicit EvdevControllerHub(std::string directory = "/dev/input",
                                    std::string sysfsRoot = "/sys/class/input");

        /** @brief Closes every controller and the watch. */
        ~EvdevControllerHub();

        EvdevControllerHub(const EvdevControllerHub&) = delete;
        EvdevControllerHub& operator=(const EvdevControllerHub&) = delete;

        /**
         * @brief Opens every controller present and starts watching for more.
         *
         * @return False when the directory cannot be read at all; the hub then simply has no
         * controllers, which is an answer rather than an error.
         */
        bool Start();

        /** @brief Closes every controller and stops watching. */
        void Stop();

        /** @brief Gets whether Start() succeeded and Stop() has not been called. @return The state. */
        [[nodiscard]] bool IsStarted() const { return started_; }

        /** @brief Picks up connections and disconnections, and reads every controller's events. */
        void Pump();

        /**
         * @brief Moves the events produced since the last call to the end of @p destination.
         *
         * @param destination Receives `DeviceEvent`, `ControllerButtonEvent` and
         * `ControllerAxisEvent` values, in the order they happened.
         */
        void TakeEvents(std::vector<PlatformEvent>& destination);

        /** @brief Gets the open controllers, in the order they connected. @return The controllers. */
        [[nodiscard]] const std::vector<std::unique_ptr<Controller>>& GetControllers() const
        {
            return controllers_;
        }

        /** @brief Finds a controller by id. @param id The id. @return The controller, or null. */
        [[nodiscard]] Controller* FindById(DeviceId id) const;

        /** @brief Finds the gamepad in a slot. @param slot The slot. @return The controller, or null. */
        [[nodiscard]] Controller* FindBySlot(int slot) const;

    private:
        void Scan();
        void ReadWatch();
        void TryOpen(const std::string& name);
        void Close(const std::string& name);
        void Remove(std::size_t index);
        void ApplyRaw(Controller& controller, const input_event& event);
        void Push(PlatformEvent event);

        std::string directory_;
        std::string sysfsRoot_;
        bool started_ = false;
        int watch_ = -1;
        std::chrono::steady_clock::time_point nextScan_{};
        DeviceId nextId_ = 1;
        std::vector<std::unique_ptr<Controller>> controllers_;
        // Nodes that could not be opened (someone else's keyboard, mostly). Not retried on every
        // pump; a permission change (IN_ATTRIB) takes a node off the list.
        std::set<std::string> refused_;
        std::deque<PlatformEvent> pending_;
        std::vector<input_event> scratch_;
        std::vector<EvdevGamepadChange> changes_;
    };

    /** @brief `IPlatformGamepad` over the hub: four XNA player slots. */
    class EvdevGamepad final : public IPlatformGamepad
    {
    public:
        /** @brief Serves the hub's gamepads. @param hub The hub. */
        explicit EvdevGamepad(EvdevControllerHub& hub);

        /** @brief Pumps the hub and publishes every slot's snapshot. */
        void Update() override;
        /** @brief Gets the slot count. @return Four, XNA's player count. */
        [[nodiscard]] int GetCount() const override { return GamepadSlotCount; }
        /** @brief Gets a slot's snapshot. @param index The slot. @return The snapshot. */
        [[nodiscard]] const GamepadSnapshot& GetSnapshot(int index) const override;
        /** @brief Gets a slot's device name. @param index The slot. @return The name, or empty. */
        [[nodiscard]] std::string GetName(int index) const override;
        /** @brief Gets a slot's capabilities. @param index The slot. @return The capabilities. */
        [[nodiscard]] const GamepadCapabilities& GetCapabilities(int index) const override;
        /** @brief Gets a slot's identity. @param index The slot. @return The identity. */
        [[nodiscard]] const GamepadInfo& GetInfo(int index) const override;
        /**
         * @brief Rumbles a slot's pad.
         * @param index The slot.
         * @param lowFrequency Strong motor, [0, 1].
         * @param highFrequency Weak motor, [0, 1].
         * @param durationMilliseconds How long; 0 until changed.
         * @return False for an empty slot or a pad that cannot rumble; never throws.
         */
        bool SetRumble(int index, float lowFrequency, float highFrequency,
                       std::uint32_t durationMilliseconds) override;
        /** @brief Trigger motors are not reachable through evdev. @return False. */
        bool SetTriggerRumble(int index, float left, float right,
                              std::uint32_t durationMilliseconds) override;
        /** @brief Light bars are not reachable through evdev. @return False. */
        bool SetLightBar(int index, std::uint8_t red, std::uint8_t green, std::uint8_t blue) override;
        /** @brief Motion sensors are not exposed here. @return False, with `reading` zeroed. */
        [[nodiscard]] bool TryGetSensor(int index, GamepadSensor sensor,
                                        GamepadSensorReading& reading) override;
        /** @brief Gets the player indicator. @return -1: evdev has none. */
        [[nodiscard]] int GetPlayerIndex(int index) const override;
        /** @brief Sets the player indicator. @return False: evdev has none. */
        bool SetPlayerIndex(int index, int playerIndex) override;
        /** @brief Gets battery state. @return Error/-1 for an empty slot, Unknown/-1 otherwise. */
        [[nodiscard]] GamepadPowerInfo GetPowerInfo(int index) const override;
        /**
         * @brief Gets the label printed on a face button.
         * @param index The slot.
         * @param button The (positional) button.
         * @return The label for the pad's family, or `Unknown`.
         */
        [[nodiscard]] GamepadButtonLabel GetButtonLabel(int index, GamepadButton button) const override;
        /** @brief Touchpads are not exposed here. @return 0. */
        [[nodiscard]] int GetTouchpadCount(int index) const override;
        /** @brief Touchpads are not exposed here. @return 0. */
        [[nodiscard]] int GetTouchpadFingerCount(int index, int touchpad) const override;
        /** @brief Touchpads are not exposed here. @return False, with `finger` zeroed. */
        [[nodiscard]] bool TryGetTouchpadFinger(int index, int touchpad, int fingerIndex,
                                                GamepadTouchpadFinger& finger) const override;

    private:
        [[nodiscard]] bool IsValid(int index) const;

        EvdevControllerHub& hub_;
        std::array<GamepadSnapshot, GamepadSlotCount> snapshots_{};
        std::array<GamepadCapabilities, GamepadSlotCount> capabilities_{};
        std::array<GamepadInfo, GamepadSlotCount> info_{};
        std::array<DeviceId, GamepadSlotCount> occupant_{};
    };

    /** @brief `IPlatformJoystick` over the hub: every controller, raw and unmapped. */
    class EvdevJoystick final : public IPlatformJoystick
    {
    public:
        /** @brief Serves the hub's controllers. @param hub The hub. */
        explicit EvdevJoystick(EvdevControllerHub& hub);

        /** @brief Pumps the hub and publishes every controller's snapshot. */
        void Update() override;
        /** @brief Gets every connected controller, in id order. @return The descriptors. */
        [[nodiscard]] std::vector<JoystickInfo> GetJoysticks() const override;
        /** @brief Gets whether an id names an open controller. @param id The id. @return The answer. */
        [[nodiscard]] bool IsConnected(DeviceId id) const override;
        /** @brief Gets a controller's shape and identity. @param id The id. @return The capabilities. */
        [[nodiscard]] JoystickCapabilities GetCapabilities(DeviceId id) const override;
        /** @brief Gets a controller's last published state. @param id The id. @return The snapshot. */
        [[nodiscard]] JoystickSnapshot GetSnapshot(DeviceId id) const override;

    private:
        EvdevControllerHub& hub_;
        std::map<DeviceId, JoystickSnapshot> published_;
    };

    /**
     * @brief Formats a device identity as a 32-digit hexadecimal GUID.
     *
     * The USB-style layout controller databases key on: bus, vendor, product and version as
     * little-endian 16-bit fields at bytes 0, 4, 8 and 12, zeroes elsewhere.
     *
     * @param description The device.
     * @return The GUID.
     */
    [[nodiscard]] std::string FormatEvdevGuid(const EvdevDescription& description);

    /**
     * @brief Combines one X/Y pair of hat axes into a POV position.
     *
     * @param x The horizontal hat axis: -1 left, 1 right.
     * @param y The vertical hat axis: -1 up, 1 down.
     * @return The position.
     */
    [[nodiscard]] JoystickHat EvdevHatPosition(int x, int y);

} // namespace CNA::Platform::Linux
