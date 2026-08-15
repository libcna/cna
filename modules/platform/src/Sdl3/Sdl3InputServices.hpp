// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"

#include <array>
#include <map>
#include <vector>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief SDL3-backed keyboard state.
     *
     * Snapshot-shaped by construction: `Update()` reads SDL's whole key array once per frame and
     * the caller then reads a local structure. `cnaplatform.md`'s input-snapshot rule is what
     * this shape enforces — thousands of `IsKeyDown` calls must not become thousands of platform
     * calls.
     */
    class Sdl3Keyboard final : public IPlatformKeyboard
    {
    public:
        /** @brief Refreshes the snapshot from SDL's current key state. */
        void Update() override;
        /** @brief Gets the most recent snapshot. @return State as of the last Update(). */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override;
        /** @brief Gets whether a keyboard is connected. @return True if at least one is present. */
        [[nodiscard]] bool HasKeyboard() const override;
        /** @brief Resolves a physical key through SDL's active layout. */
        [[nodiscard]] KeyCode GetKeyFromScancode(Scancode scancode) const override;
        /** @brief Gets SDL's stable physical-key name. */
        [[nodiscard]] std::string GetScancodeName(Scancode scancode) const override;
        /** @brief Resolves SDL's physical-key name. */
        [[nodiscard]] Scancode GetScancodeFromName(const std::string& name) const override;
        /** @brief Gets SDL's active-layout key name. */
        [[nodiscard]] std::string GetKeyName(Scancode scancode) const override;
        /** @brief Resolves SDL's active-layout key name. */
        [[nodiscard]] KeyCode GetKeyFromName(const std::string& name) const override;

    private:
        KeyboardSnapshot snapshot_;
    };

    /** @brief SDL3-backed mouse state and cursor control. */
    class Sdl3Mouse final : public IPlatformMouse
    {
    public:
        /** @brief Refreshes the snapshot from SDL's current mouse state. */
        void Update() override;
        /** @brief Gets the most recent snapshot. @return State as of the last Update(). */
        [[nodiscard]] const MouseSnapshot& GetSnapshot() const override;
        /** @brief Returns and clears relative displacement. @return Pending displacement. */
        [[nodiscard]] MouseDelta ConsumeRelativeDelta() override;
        /** @brief Accumulates event-only mouse state such as wheel movement. @param event Event. */
        void ObserveEvent(const PlatformEvent& event);
        /**
         * @brief Warps the pointer within a window.
         * @param window The window id to position within, or zero.
         * @param x Target x in client coordinates.
         * @param y Target y in client coordinates.
         */
        void SetPosition(WindowId window, int x, int y) override;
        /** @brief Shows or hides the cursor. @param visible True to show it. */
        void SetCursorVisible(bool visible) override;
        /** @brief Sets the cursor shape. @param cursor The shape to display. */
        void SetCursor(SystemCursor cursor) override;
        /** @brief Creates and sets an RGBA image cursor. @param cursor Image and hot spot. */
        void SetCursor(const CursorImage& cursor) override;
        /**
         * @brief Enables or disables pointer lock.
         * @param window The window to capture.
         * @param enabled True to capture the pointer.
         */
        void SetRelativeMode(WindowId window, bool enabled) override;
        /** @brief Gets whether pointer lock is active. @return True if captured. */
        [[nodiscard]] bool IsRelativeMode() const override;
        /**
         * @brief Captures the pointer so motion is reported outside any window.
         * @param enabled True to capture.
         * @return True if SDL accepted the change.
         */
        bool SetCapture(bool enabled) override;
        /**
         * @brief Reads the pointer position in desktop coordinates.
         * @param x Receives desktop x; untouched on false.
         * @param y Receives desktop y; untouched on false.
         * @return True if a position was available.
         */
        [[nodiscard]] bool TryGetGlobalPosition(float& x, float& y) const override;
        /**
         * @brief Warps the pointer in desktop coordinates.
         * @param x Target desktop x.
         * @param y Target desktop y.
         * @return True if SDL accepted the warp.
         */
        bool SetGlobalPosition(float x, float y) override;

        /** @brief Releases any cursor this service created. */
        ~Sdl3Mouse() override;

    private:
        MouseSnapshot snapshot_;
        float relativeDeltaX_ = 0.0f;
        float relativeDeltaY_ = 0.0f;
        void* activeCursor_ = nullptr;
        bool relativeMode_ = false;

        void InstallCursor(void* cursor);
    };

    /** @brief SDL3-backed gamepad reading and rumble. */
    class Sdl3Gamepad final : public IPlatformGamepad
    {
    public:
        /** @brief Closes every gamepad this service opened. */
        ~Sdl3Gamepad() override;

        /** @brief Reconciles stable player slots and refreshes every snapshot. */
        void Update() override;
        /** @brief Gets the number of slots. @return The slot count. */
        [[nodiscard]] int GetCount() const override;
        /**
         * @brief Gets one slot's snapshot.
         * @param index The slot to read.
         * @return The state; a slot out of range reports not connected rather than throwing.
         */
        [[nodiscard]] const GamepadSnapshot& GetSnapshot(int index) const override;
        /**
         * @brief Gets a device's name.
         * @param index The slot to read.
         * @return The name, or empty when the slot is empty.
         */
        [[nodiscard]] std::string GetName(int index) const override;
        /** @brief Gets one slot's cached capabilities. @param index Slot. @return Capabilities. */
        [[nodiscard]] const GamepadCapabilities& GetCapabilities(int index) const override;
        /** @brief Gets one slot's cached identity. @param index Slot. @return Identity. */
        [[nodiscard]] const GamepadInfo& GetInfo(int index) const override;
        /**
         * @brief Starts rumble on a device.
         * @param index The slot to rumble.
         * @param lowFrequency Low-frequency motor strength in [0, 1].
         * @param highFrequency High-frequency motor strength in [0, 1].
         * @param durationMilliseconds How long to rumble.
         * @return True if the device accepted it.
         */
        bool SetRumble(int index, float lowFrequency, float highFrequency,
                       std::uint32_t durationMilliseconds) override;
        /** @brief Starts trigger rumble. */
        bool SetTriggerRumble(int index, float left, float right,
                              std::uint32_t durationMilliseconds) override;
        /** @brief Sets the RGB light bar. */
        bool SetLightBar(int index, std::uint8_t red, std::uint8_t green,
                         std::uint8_t blue) override;
        /** @brief Reads a pad-local motion sensor. */
        [[nodiscard]] bool TryGetSensor(int index, GamepadSensor sensor,
                                        GamepadSensorReading& reading) override;
        /** @brief Gets the native player indicator. */
        [[nodiscard]] int GetPlayerIndex(int index) const override;
        /** @brief Sets the native player indicator. */
        bool SetPlayerIndex(int index, int playerIndex) override;
        /** @brief Gets battery state. */
        [[nodiscard]] GamepadPowerInfo GetPowerInfo(int index) const override;
        /** @brief Gets a face-button glyph label. */
        [[nodiscard]] GamepadButtonLabel GetButtonLabel(int index,
                                                        GamepadButton button) const override;
        /** @brief Gets the touchpad count. */
        [[nodiscard]] int GetTouchpadCount(int index) const override;
        /** @brief Gets one touchpad's finger count. */
        [[nodiscard]] int GetTouchpadFingerCount(int index, int touchpad) const override;
        /** @brief Reads one touchpad contact. */
        [[nodiscard]] bool TryGetTouchpadFinger(int index, int touchpad, int fingerIndex,
                                                GamepadTouchpadFinger& finger) const override;

    private:
        void CloseAll();
        void CloseSlot(std::size_t slot);
        [[nodiscard]] void* GetHandle(int index) const;

        std::array<void*, GamepadSlotCount> handles_{};
        std::array<DeviceId, GamepadSlotCount> deviceIds_{};
        std::array<GamepadSnapshot, GamepadSlotCount> snapshots_{};
        std::array<GamepadCapabilities, GamepadSlotCount> capabilities_{};
        std::array<GamepadInfo, GamepadSlotCount> infos_{};
    };

    /** @brief SDL3-backed raw, unmapped joystick snapshots. */
    class Sdl3Joystick final : public IPlatformJoystick
    {
    public:
        /** @brief Closes every joystick this service opened. */
        ~Sdl3Joystick() override;

        /** @brief Reconciles attached ids and publishes one snapshot per device. */
        void Update() override;
        /** @brief Gets connected devices in ascending id order. */
        [[nodiscard]] std::vector<JoystickInfo> GetJoysticks() const override;
        /** @brief Gets whether an id currently has an opened handle. */
        [[nodiscard]] bool IsConnected(DeviceId id) const override;
        /** @brief Gets a device's cached shape and power state. */
        [[nodiscard]] JoystickCapabilities GetCapabilities(DeviceId id) const override;
        /** @brief Gets a device's last frame-stable raw state. */
        [[nodiscard]] JoystickSnapshot GetSnapshot(DeviceId id) const override;

        /** @brief Applies one mapped joystick hotplug event before it reaches consumers. */
        void ObserveEvent(const DeviceEvent& event);

    private:
        struct Device
        {
            void* handle = nullptr;
            JoystickInfo info;
            JoystickCapabilities capabilities;
            JoystickSnapshot snapshot;
        };

        void Open(DeviceId id);
        void Close(DeviceId id);
        void CloseAll();
        void Poll(Device& device);

        std::map<DeviceId, Device> devices_;
    };

    /** @brief SDL3-backed text input and IME control. */
    class Sdl3TextInput final : public IPlatformTextInput
    {
    public:
        /** @brief Begins text input for a window with a purpose hint. */
        void Start(WindowId window, TextInputType type) override;
        /** @brief Ends text input. @param window The window to stop for. */
        void Stop(WindowId window) override;
        /** @brief Gets whether text input is active for a window. */
        [[nodiscard]] bool IsActive(WindowId window) const override;
        /** @brief Gets whether the screen keyboard is shown for a window. */
        [[nodiscard]] bool IsScreenKeyboardShown(WindowId window) const override;
        /**
         * @brief Tells the input method where the edited text is.
         * @param window The window being edited in.
         * @param area The area to avoid covering.
         */
        void SetInputArea(WindowId window, const TextInputArea& area) override;
    };

    /** @brief SDL3-backed enumeration of attached input devices. */
    class Sdl3InputDevices final : public IPlatformInputDevices
    {
    public:
        /**
         * @brief Lists the attached devices of one class.
         * @param kind Which class to list.
         * @return The attached devices; empty when none are attached.
         */
        [[nodiscard]] std::vector<InputDeviceInfo> GetDevices(InputDeviceKind kind) const override;
        /**
         * @brief Gets whether at least one device of a class is attached.
         * @param kind Which class to test for.
         * @return True if at least one is attached.
         */
        [[nodiscard]] bool HasDevice(InputDeviceKind kind) const override;
    };

} // namespace CNA::Platform::Sdl3
