// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#include <string>

namespace CNA::Platform {

    /** @brief A rectangle in window coordinates. */
    struct WindowBounds
    {
        /** @brief Left edge. */
        int x = 0;
        /** @brief Top edge. */
        int y = 0;
        /** @brief Width. */
        int width = 0;
        /** @brief Height. */
        int height = 0;
    };

    /** @brief A size in pixels. */
    struct WindowSize
    {
        /** @brief Width. */
        int width = 0;
        /** @brief Height. */
        int height = 0;
    };

    /**
     * @brief One window represented by a platform implementation.
     *
     * The surface `Microsoft::Xna::Framework::GameWindow` is built on. Every member here is
     * something `GameWindow.cpp` or `GraphicsDeviceManager.cpp` does today through SDL; nothing
     * is speculative.
     *
     * A window returned by `IPlatform::CreateWindow` owns its native window and destroys it with
     * the wrapper. A wrapper returned by `IPlatform::AdoptWindow` is deliberately non-owning: the
     * external caller must keep the native window alive. A renderer holds the `NativeWindowHandle`
     * this exposes, not either kind of wrapper.
     */
    class IPlatformWindow
    {
    public:
        /** @brief Destroys the wrapper and releases the native window when this wrapper owns it. */
        virtual ~IPlatformWindow() = default;

        /**
         * @brief Gets the id identifying this window in `PlatformEvent`s.
         *
         * @return A non-zero id, stable for the window's lifetime.
         */
        [[nodiscard]] virtual WindowId GetId() const = 0;

        /**
         * @brief Gets the native handle a renderer initializes against.
         *
         * @return The native handle; its `system` is `Headless` when the platform has no native
         * window, in which case a renderer requiring one must refuse.
         */
        [[nodiscard]] virtual NativeWindowHandle GetNativeHandle() const = 0;

        /**
         * @brief Gets the window's title.
         *
         * @return The current title.
         */
        [[nodiscard]] virtual std::string GetTitle() const = 0;

        /**
         * @brief Sets the window's title.
         *
         * @param title The new title.
         */
        virtual void SetTitle(const std::string& title) = 0;

        /**
         * @brief Gets the client area in logical units.
         *
         * @return The client bounds.
         */
        [[nodiscard]] virtual WindowBounds GetClientBounds() const = 0;

        /**
         * @brief Gets the drawable size in physical pixels.
         *
         * May exceed the logical client size under high DPI. A renderer sizes its swapchain from
         * this, never from the logical bounds — that difference is the whole reason both exist.
         *
         * @return The drawable size in pixels.
         */
        [[nodiscard]] virtual WindowSize GetPixelSize() const = 0;

        /**
         * @brief Sets the client area size in logical units.
         *
         * @param width The new client width.
         * @param height The new client height.
         */
        virtual void SetSize(int width, int height) = 0;

        /**
         * @brief Gets the display scale factor for this window.
         *
         * @return The scale, where 1.0 means one logical unit per physical pixel. Always 1.0 on
         * platforms that report no `HighDpi` capability.
         */
        [[nodiscard]] virtual float GetDisplayScale() const = 0;

        /**
         * @brief Gets whether the user may resize the window.
         *
         * @return True when user resizing is enabled.
         */
        [[nodiscard]] virtual bool IsResizable() const = 0;

        /**
         * @brief Sets whether the user may resize the window.
         *
         * @param resizable True to allow resizing.
         */
        virtual void SetResizable(bool resizable) = 0;

        /**
         * @brief Gets whether the window is drawn without decorations.
         *
         * @return True when the border is hidden.
         */
        [[nodiscard]] virtual bool IsBorderless() const = 0;

        /**
         * @brief Sets whether the window is drawn without decorations.
         *
         * @param borderless True to remove the border.
         */
        virtual void SetBorderless(bool borderless) = 0;

        /**
         * @brief Sets how the window occupies the display.
         *
         * @param mode The requested mode.
         * @throws PlatformNotSupportedException If @p mode is `BorderlessFullscreen` and the
         * platform reports no `BorderlessFullscreen` capability.
         */
        virtual void SetFullscreenMode(WindowFullscreenMode mode) = 0;

        /**
         * @brief Gets how the window currently occupies the display.
         *
         * @return The current mode.
         */
        [[nodiscard]] virtual WindowFullscreenMode GetFullscreenMode() const = 0;

        /** @brief Makes the window visible. */
        virtual void Show() = 0;

        /** @brief Hides the window without destroying it. */
        virtual void Hide() = 0;

        /** @brief Minimises the window. */
        virtual void Minimize() = 0;

        /** @brief Maximises the window. */
        virtual void Maximize() = 0;

        /** @brief Returns the window from minimised or maximised to its normal state. */
        virtual void Restore() = 0;

        /**
         * @brief Blocks until pending window state changes have been applied by the system.
         *
         * Window state changes are asynchronous on most systems: a `SetSize` followed
         * immediately by `GetClientBounds` may report the old size. Call this when the next step
         * depends on the change having landed.
         */
        virtual void Sync() = 0;

        /**
         * @brief Gets whether the window currently has keyboard focus.
         *
         * @return True if focused.
         */
        [[nodiscard]] virtual bool HasFocus() const = 0;

        /**
         * @brief Gets whether the window is currently minimised.
         *
         * @return True if minimised.
         */
        [[nodiscard]] virtual bool IsMinimized() const = 0;

        /**
         * @brief Gets the name of the display this window is currently on.
         *
         * Backs `GameWindow::ScreenDeviceName`.
         *
         * @return The display's name, or an empty string when the platform has no displays.
         */
        [[nodiscard]] virtual std::string GetDisplayName() const = 0;
    };

} // namespace CNA::Platform
