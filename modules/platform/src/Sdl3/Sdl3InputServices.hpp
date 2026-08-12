// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformGamepad.hpp"
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
        /**
         * @brief Warps the pointer within a window.
         * @param window The window to position within.
         * @param x Target x in client coordinates.
         * @param y Target y in client coordinates.
         */
        void SetPosition(IPlatformWindow& window, int x, int y) override;
        /** @brief Shows or hides the cursor. @param visible True to show it. */
        void SetCursorVisible(bool visible) override;
        /** @brief Sets the cursor shape. @param cursor The shape to display. */
        void SetCursor(SystemCursor cursor) override;
        /** @brief Enables or disables pointer lock. @param enabled True to capture the pointer. */
        void SetRelativeMode(bool enabled) override;
        /** @brief Gets whether pointer lock is active. @return True if captured. */
        [[nodiscard]] bool IsRelativeMode() const override;

        /** @brief Releases any cursor this service created. */
        ~Sdl3Mouse() override;

    private:
        MouseSnapshot snapshot_;
        void* activeCursor_ = nullptr;
        bool relativeMode_ = false;
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

} // namespace CNA::Platform::Sdl3
