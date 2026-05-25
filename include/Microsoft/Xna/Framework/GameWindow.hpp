#pragma once

#include <cstdint>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"

struct SDL_Window;

namespace Microsoft::Xna::Framework {

    class Game;

    /// Represents the game window and exposes its platform-backed window state.
    class GraphicsDeviceManager;

    class GameWindow : public System::Object {
        friend class Game;
        friend class GraphicsDeviceManager;

    public:
        using String = SharpRuntime::String;
        using intcs = SharpRuntime::intcs;

        /// Raised when the client area size changes.
        System::EventHandler<System::EventArgs> ClientSizeChanged;

        /// Raised when the display orientation changes.
        System::EventHandler<System::EventArgs> OrientationChanged;

        /// Raised when the screen device name changes.
        System::EventHandler<System::EventArgs> ScreenDeviceNameChanged;

        /// Creates a window wrapper without an attached SDL window.
        GameWindow();

        /// Creates a window wrapper for an existing SDL window.
        explicit GameWindow(SDL_Window* window);

        ~GameWindow() override = default;

        /// Gets whether the user may resize the window.
        [[nodiscard]] bool getAllowUserResizingProperty() const;

        /// Sets whether the user may resize the window.
        void setAllowUserResizingProperty(bool value);

        /// Gets the client bounds of the window.
        [[nodiscard]] Rectangle getClientBoundsProperty() const;

        /// Gets the current display orientation.
        [[nodiscard]] DisplayOrientation getCurrentOrientationProperty() const;

        /// Gets the native window handle as an integer pointer value.
        [[nodiscard]] SharpRuntime::IntPtr getHandleProperty() const;

        /// Gets the name of the screen/display containing this window.
        [[nodiscard]] const String& getScreenDeviceNameProperty() const;

        /// Gets the cached title of the window.
        [[nodiscard]] const String& getTitleProperty() const;

        /// Sets the title of the window and updates the native SDL window.
        void setTitleProperty(const String& title);

        /// Gets whether the window border is hidden.
        [[nodiscard]] bool getIsBorderlessEXTProperty() const;

        /// Shows or hides the window border when supported by SDL.
        void setIsBorderlessEXTProperty(bool value);

        /// Begins a fullscreen/windowed device change.
        void BeginScreenDeviceChange(bool willBeFullScreen);

        /// Ends a device change using the specified display name and client size.
        void EndScreenDeviceChange(const String& screenDeviceName, intcs clientWidth, intcs clientHeight);

        /// Ends a device change using the current client size.
        void EndScreenDeviceChange(const String& screenDeviceName);

        [[nodiscard]] const String& GetTypeName() const override;

    protected:
        /// Called when the window is activated.
        void OnActivated();

        /// Raises ClientSizeChanged.
        void OnClientSizeChanged();

        /// Called when the window is deactivated.
        void OnDeactivated();

        /// Raises OrientationChanged.
        void OnOrientationChanged();

        /// Called when the window should repaint.
        void OnPaint();

        /// Raises ScreenDeviceNameChanged.
        void OnScreenDeviceNameChanged();

        /// Stores supported orientations and adjusts current orientation if needed.
        void SetSupportedOrientations(DisplayOrientation orientations);

        /// Applies the title to the native window.
        virtual void SetTitle(const String& title);

    private:
        SDL_Window* window_;
        String title_;
        String screenDeviceName_;
        Rectangle clientBounds_;
        DisplayOrientation currentOrientation_;
        DisplayOrientation supportedOrientations_;
        bool allowUserResizing_;
        bool isBorderless_;
        bool pendingFullScreen_;
        bool hasPendingScreenDeviceChange_;

        void setWindowInternal(SDL_Window* window);
        void setCurrentOrientationProperty(DisplayOrientation value);
        void updateFromSDL();
        void refreshCachedSDLState(bool raiseEvents);
        [[nodiscard]] Rectangle queryClientBoundsFromSDL() const;
        [[nodiscard]] String queryScreenDeviceNameFromSDL() const;
        [[nodiscard]] DisplayOrientation orientationFromBounds(const Rectangle& bounds) const;
        [[nodiscard]] bool orientationIsSupported(DisplayOrientation orientation) const;
    };

} // namespace Microsoft::Xna::Framework
