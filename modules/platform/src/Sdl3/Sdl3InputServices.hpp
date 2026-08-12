// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"

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

        /** @brief Rescans connected devices and refreshes every snapshot. */
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

    private:
        void CloseAll();

        std::vector<void*> handles_;
        std::vector<GamepadSnapshot> snapshots_;
    };

    /** @brief SDL3-backed text input and IME control. */
    class Sdl3TextInput final : public IPlatformTextInput
    {
    public:
        /** @brief Begins text input. @param window The window to receive text events. */
        void Start(IPlatformWindow& window) override;
        /** @brief Ends text input. @param window The window to stop for. */
        void Stop(IPlatformWindow& window) override;
        /** @brief Gets whether text input is active. @return True while active. */
        [[nodiscard]] bool IsActive() const override;
        /**
         * @brief Tells the input method where the edited text is.
         * @param window The window being edited in.
         * @param area The area to avoid covering.
         */
        void SetInputArea(IPlatformWindow& window, const TextInputArea& area) override;

    private:
        bool active_ = false;
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
