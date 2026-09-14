// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/PlatformCapabilities.hpp"

#include "Win32Common.hpp"

#include <map>
#include <string>

namespace CNA::Platform::Win32 {

    class Win32Window;

    /**
     * @brief What the input and system services need from the platform that owns them.
     *
     * Services never own windows and never hold an `HWND` of their own; they ask for the one they
     * need at the moment they need it. That is what keeps window lifetime a single concern of the
     * platform's registry, so a service cannot outlive a window it cached.
     */
    class Win32PlatformAccess
    {
    public:
        /** @brief Destroys the accessor. */
        virtual ~Win32PlatformAccess() = default;

        /**
         * @brief Gets the window that currently has keyboard focus.
         *
         * @return The focused window, or null when none of this platform's windows has focus.
         */
        [[nodiscard]] virtual Win32Window* GetFocusedWindow() = 0;

        /**
         * @brief Finds a window by its stable id.
         *
         * @param window The id to resolve.
         * @return The window, or null when the id names no live window of this platform.
         */
        [[nodiscard]] virtual Win32Window* FindWindow(WindowId window) = 0;

        /**
         * @brief Gets any live window of this platform.
         *
         * Used by desktop-scoped operations that need some window to anchor to -- cursor
         * confinement, a dialog parent -- and do not care which.
         *
         * @return A live window, or null when the platform has none.
         */
        [[nodiscard]] virtual Win32Window* GetAnyWindow() = 0;

        /**
         * @brief Gets this platform's capability set.
         *
         * @return The capabilities, so a service can raise the refusal a false one promises.
         */
        [[nodiscard]] virtual PlatformCapabilities GetPlatformCapabilities() const = 0;

        /**
         * @brief Gets this platform's name, for refusal messages.
         *
         * @return `"Win32"`.
         */
        [[nodiscard]] virtual const std::string& GetPlatformName() const = 0;
    };

    /**
     * @brief Reads the whole keyboard through `GetKeyboardState`.
     *
     * One call per frame rather than one per key: `Keyboard::GetState()` is a level query a game
     * may consult hundreds of times per frame, and the contract's snapshot rule exists precisely
     * so those do not become hundreds of platform calls.
     */
    class Win32Keyboard final : public IPlatformKeyboard
    {
    public:
        /** @brief Refreshes the snapshot from the current keyboard state. */
        void Update() override;

        /** @brief Gets the most recent snapshot. @return The state as of the last Update(). */
        [[nodiscard]] const KeyboardSnapshot& GetSnapshot() const override;

        /** @brief Gets whether a keyboard is present. @return True when one is attached. */
        [[nodiscard]] bool HasKeyboard() const override;

        /** @brief Resolves a physical key through the active layout. @param scancode The key. @return The layout's key. */
        [[nodiscard]] KeyCode GetKeyFromScancode(Scancode scancode) const override;

        /** @brief Gets CNA's stable name for a physical key. @param scancode The key. @return The name. */
        [[nodiscard]] std::string GetScancodeName(Scancode scancode) const override;

        /** @brief Resolves a stable physical-key name. @param name The name. @return The key. */
        [[nodiscard]] Scancode GetScancodeFromName(const std::string& name) const override;

        /** @brief Gets the active layout's name for a physical key. @param scancode The key. @return The name. */
        [[nodiscard]] std::string GetKeyName(Scancode scancode) const override;

        /** @brief Resolves an active-layout key name. @param name The name. @return The key. */
        [[nodiscard]] KeyCode GetKeyFromName(const std::string& name) const override;

    private:
        KeyboardSnapshot snapshot_;
    };

    /**
     * @brief Reads pointer state and controls the cursor.
     *
     * Absolute position, buttons and wheel totals come from the focused window's message
     * translation, because Windows has no pollable "where is the pointer in this client area"
     * that agrees with the events a game has already seen. Relative displacement comes from Raw
     * Input, which is the only source that keeps reporting once the pointer is confined.
     */
    class Win32Mouse final : public IPlatformMouse
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param access The owning platform.
         */
        explicit Win32Mouse(Win32PlatformAccess& access);

        /** @brief Destroys the service, leaving relative mode if it is still active. */
        ~Win32Mouse() override;

        /** @brief Refreshes pollable state after the frame's event pump. */
        void Update() override;

        /** @brief Gets the most recent snapshot. @return The state as of the last Update(). */
        [[nodiscard]] const MouseSnapshot& GetSnapshot() const override;

        /** @brief Returns and clears accumulated relative motion. @return The displacement. */
        [[nodiscard]] MouseDelta ConsumeRelativeDelta() override;

        /** @brief Warps the pointer within a window. @param window The window. @param x Client x. @param y Client y. */
        void SetPosition(WindowId window, int x, int y) override;

        /** @brief Shows or hides the cursor. @param visible True to show it. */
        void SetCursorVisible(bool visible) override;

        /** @brief Sets a system cursor shape. @param cursor The shape. */
        void SetCursor(SystemCursor cursor) override;

        /** @brief Sets a custom image cursor. @param cursor The image and hot spot. */
        void SetCursor(const CursorImage& cursor) override;

        /** @brief Enables or disables relative mode. @param window The window to capture. @param enabled True to capture. */
        void SetRelativeMode(WindowId window, bool enabled) override;

        /** @brief Gets whether relative mode is active. @return True while the pointer is captured. */
        [[nodiscard]] bool IsRelativeMode() const override;

        /** @brief Captures the pointer across the desktop. @param enabled True to capture. @return True when applied. */
        bool SetCapture(bool enabled) override;

        /** @brief Reads the desktop pointer position. @param x Receives x. @param y Receives y. @return True on success. */
        [[nodiscard]] bool TryGetGlobalPosition(float& x, float& y) const override;

        /** @brief Warps the pointer in desktop coordinates. @param x Target x. @param y Target y. @return True when applied. */
        bool SetGlobalPosition(float x, float y) override;

        /**
         * @brief Accumulates raw pointer displacement reported by a window.
         *
         * @param deltaX Horizontal displacement.
         * @param deltaY Vertical displacement.
         */
        void AccumulateRawDelta(int deltaX, int deltaY);

        /**
         * @brief Re-applies the active cursor shape.
         *
         * Windows resets the cursor to the window class's every time the pointer moves over the
         * client area, so `WM_SETCURSOR` has to put the chosen one back.
         *
         * @return True when a shape was applied.
         */
        bool ApplyCursor();

    private:
        void ReleaseOwnedCursor();

        Win32PlatformAccess* access_;
        MouseSnapshot snapshot_;
        int relativeX_ = 0;
        int relativeY_ = 0;
        bool relativeMode_ = false;
        WindowId relativeWindow_ = 0;
        bool cursorVisible_ = true;
        HCURSOR cursor_ = nullptr;
        /// Non-null only for a cursor this service created and must destroy.
        HCURSOR ownedCursor_ = nullptr;
    };

    /** @brief Controls committed-text delivery per window. */
    class Win32TextInput final : public IPlatformTextInput
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param access The owning platform.
         */
        explicit Win32TextInput(Win32PlatformAccess& access);

        /** @brief Begins text input. @param window The window. @param type The kind of text. */
        void Start(WindowId window, TextInputType type) override;

        /** @brief Ends text input. @param window The window. */
        void Stop(WindowId window) override;

        /** @brief Gets whether text input is active. @param window The window. @return True while active. */
        [[nodiscard]] bool IsActive(WindowId window) const override;

        /** @brief Gets whether an on-screen keyboard is shown. @param window The window. @return Always false. */
        [[nodiscard]] bool IsScreenKeyboardShown(WindowId window) const override;

        /** @brief Records where the edited text is. @param window The window. @param area The area. */
        void SetInputArea(WindowId window, const TextInputArea& area) override;

    private:
        Win32PlatformAccess* access_;
        std::map<WindowId, TextInputArea> areas_;
    };

    /** @brief Enumerates attached keyboards and pointers through Raw Input. */
    class Win32InputDevices final : public IPlatformInputDevices
    {
    public:
        /** @brief Gets attached devices of one class. @param kind The class. @return The devices. */
        [[nodiscard]] std::vector<InputDeviceInfo> GetDevices(InputDeviceKind kind) const override;

        /** @brief Gets whether a class has any device. @param kind The class. @return True if attached. */
        [[nodiscard]] bool HasDevice(InputDeviceKind kind) const override;
    };

} // namespace CNA::Platform::Win32
