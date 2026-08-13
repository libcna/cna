// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"

namespace CNA::Platform
{
    class IPlatformWindow;
}

namespace CNA::Devices
{
    class DisplayInfo;
}

namespace Microsoft::Xna::Framework
{
    class Game;
    class GraphicsDeviceManager;

    /**
     * @brief Represents the game window backed by the selected platform.
     *
     * FNA defines GameWindow as abstract with per-platform subclasses; CNA keeps one concrete
     * facade and delegates its behavior to `IPlatformWindow`.
     */
    class GameWindow : public System::Object
    {
        friend class Game;
        friend class GraphicsDeviceManager;
        friend class CNA::Devices::DisplayInfo;

    public:
        /** @brief String type alias for C++ compatibility. */
        CNAEXT using String = SharpRuntime::String;
        /** @brief Integer type alias matching the C# int used in the XNA API. */
        CNAEXT using intcs = SharpRuntime::intcs;

        /** @brief Raised when the client area size changes. */
        System::EventHandler<System::EventArgs> ClientSizeChanged;

        /** @brief Raised when the display orientation changes. */
        System::EventHandler<System::EventArgs> OrientationChanged;

        /** @brief Raised when the screen device name changes. */
        System::EventHandler<System::EventArgs> ScreenDeviceNameChanged;

        /** @brief Creates a window wrapper without an attached platform window. */
        GameWindow();

        /**
         * @brief Creates a non-owning wrapper for an existing platform window.
         *
         * The caller retains ownership and must keep @p window alive for this object's lifetime.
         *
         * @param window Platform window to borrow; may be null.
         */
        CNAEXT explicit GameWindow(CNA::Platform::IPlatformWindow* window);

        /** @brief Destructor. */
        ~GameWindow() override;

        /**
         * @brief Gets whether the user may resize the window.
         * @return true if user resizing is allowed.
         */
        [[nodiscard]] bool getAllowUserResizingProperty() const;

        /**
         * @brief Sets whether the user may resize the window.
         * @param value true to allow user resizing; false to prevent it.
         */
        void setAllowUserResizingProperty(bool value);

        /**
         * @brief Gets the client bounds of the window.
         * @return A Rectangle describing the client area in screen coordinates.
         */
        [[nodiscard]] Rectangle getClientBoundsProperty() const;

        /**
         * @brief Gets the current display orientation.
         * @return The current DisplayOrientation value.
         */
        [[nodiscard]] DisplayOrientation getCurrentOrientationProperty() const;

        /**
         * @brief Gets the native window handle as an integer pointer value.
         * @return The platform window handle.
         */
        [[nodiscard]] SharpRuntime::IntPtr getHandleProperty() const;

        /**
         * @brief Gets the native window-system handle this instance wraps.
         *
         * CNA extension for renderer or interop code that needs a platform-neutral native handle.
         * The returned value owns nothing and is valid only while the borrowed platform window
         * remains alive. Callers must inspect its `system` before using a typed native field.
         *
         * @return The native handle, or an `Unknown` empty handle when no window is attached.
         */
        CNAEXT [[nodiscard]] CNA::Platform::NativeWindowHandle GetNativeWindowHandleEXT() const;

        /**
         * @brief Gets the name of the screen/display containing this window.
         * @return A const reference to the screen device name string.
         */
        [[nodiscard]] const String& getScreenDeviceNameProperty() const;

        /**
         * @brief Gets the cached title of the window.
         * @return A const reference to the window title string.
         */
        [[nodiscard]] const String& getTitleProperty() const;

        /**
         * @brief Sets the title of the window and updates the platform window.
         * @param title The new window title.
         */
        void setTitleProperty(const String& title);

        /**
         * @brief Gets whether the window border is hidden.
         * @return true if the window is borderless.
         */
        [[nodiscard]] bool getIsBorderlessEXTProperty() const;

        /**
         * @brief Shows or hides the window border when supported by the platform.
         * @param value true to remove the border; false to restore it.
         */
        void setIsBorderlessEXTProperty(bool value);

        /**
         * @brief Minimizes the window to the taskbar/dock.
         *
         * CNA extension — XNA has no minimize/restore API of its own. No-op if this
         * GameWindow wraps no platform window.
         */
        CNAEXT void MinimizeEXT();

        /**
         * @brief Restores a minimized or maximized window to its normal state.
         *
         * CNA extension — XNA has no minimize/restore API of its own. No-op if this
         * GameWindow wraps no platform window.
         */
        CNAEXT void RestoreEXT();

        /**
         * @brief Begins a fullscreen/windowed device change.
         * @param willBeFullScreen true if the new mode is fullscreen; false for windowed.
         */
        void BeginScreenDeviceChange(bool willBeFullScreen);

        /**
         * @brief Ends a device change using the specified display name and client size.
         * @param screenDeviceName The name of the display adapter.
         * @param clientWidth The new client area width in pixels.
         * @param clientHeight The new client area height in pixels.
         */
        void EndScreenDeviceChange(const String& screenDeviceName, intcs clientWidth, intcs clientHeight);

        /**
         * @brief Ends a device change using the current client size.
         * @param screenDeviceName The name of the display adapter.
         */
        void EndScreenDeviceChange(const String& screenDeviceName);

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /** @brief Called when the window is activated. */
        void OnActivated();

        /** @brief Raises ClientSizeChanged. */
        void OnClientSizeChanged();

        /** @brief Called when the window is deactivated. */
        void OnDeactivated();

        /** @brief Raises OrientationChanged. */
        void OnOrientationChanged();

        /** @brief Called when the window should repaint. */
        void OnPaint();

        /** @brief Raises ScreenDeviceNameChanged. */
        void OnScreenDeviceNameChanged();

        /**
         * @brief Stores supported orientations and adjusts current orientation if needed.
         * @param orientations Bitmask of the orientations the game supports.
         */
        void SetSupportedOrientations(DisplayOrientation orientations);

        /**
         * @brief Applies the title to the native window.
         * @param title The title string to apply.
         */
        virtual void SetTitle(const String& title);

    private:
        // GameWindow never owns the platform window. GraphicsDevice or the public constructor's
        // caller keeps it alive.
        CNA::Platform::IPlatformWindow* window_;
        SharpRuntime::IntPtr legacyHandle_;
        String title_;
        String screenDeviceName_;
        Rectangle clientBounds_;
        DisplayOrientation currentOrientation_;
        DisplayOrientation supportedOrientations_;
        bool allowUserResizing_;
        bool isBorderless_;
        bool pendingFullScreen_;
        bool hasPendingScreenDeviceChange_;

        void setWindowInternal(CNA::Platform::IPlatformWindow* window,
                               SharpRuntime::IntPtr legacyHandle);
        [[nodiscard]] CNA::Platform::IPlatformWindow* getPlatformWindowInternal() const;
        void setCurrentOrientationProperty(DisplayOrientation value);
        void updateFromPlatform();
        void refreshCachedPlatformState(bool raiseEvents);
        [[nodiscard]] Rectangle queryClientBoundsFromPlatform() const;
        [[nodiscard]] String queryScreenDeviceNameFromPlatform() const;
        [[nodiscard]] DisplayOrientation orientationFromBounds(const Rectangle& bounds) const;
        [[nodiscard]] bool orientationIsSupported(DisplayOrientation orientation) const;
    };
} // namespace Microsoft::Xna::Framework
