// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GameWindow.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

#include "CNA/Platform.hpp"
#if defined(CNA_PLATFORM_IOS)
#include "CNA/Internal/AppleOrientation.hpp"
#endif

namespace Microsoft::Xna::Framework
{
    namespace
    {
        std::runtime_error makeSdlError(const char* operation)
        {
            return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
        }

        bool hasFlag(DisplayOrientation value, DisplayOrientation flag)
        {
            return (static_cast<int>(value) & static_cast<int>(flag)) != 0;
        }

        // plan_apple.md APPLE-15. On a desktop the supported-orientation set is CNA's own
        // bookkeeping, but on iOS and Android the operating system decides whether the device may
        // rotate at all, and SDL_HINT_ORIENTATIONS is the only channel it reads: SDL's UIKit view
        // controller consults the hint every time UIKit asks which orientations the application
        // accepts. Without this, XNA's SupportedOrientations would silently mean nothing on the
        // one platform family it was designed for.
        //
        // iOS intersects this with the UISupportedInterfaceOrientations array in the bundle's
        // Info.plist (cmake/AppleInfo.iOS.plist.in), which is the outer bound; the hint can
        // narrow that set at run time but never widen it.
        void applySupportedOrientationsHint(DisplayOrientation orientations, SDL_Window* window)
        {
            if (!CNA::isMobilePlatform())
            {
                return;
            }

            std::string hint;
            if (hasFlag(orientations, DisplayOrientation::LandscapeLeft))
            {
                hint += "LandscapeLeft ";
            }
            if (hasFlag(orientations, DisplayOrientation::LandscapeRight))
            {
                hint += "LandscapeRight ";
            }
            if (hasFlag(orientations, DisplayOrientation::Portrait))
            {
                hint += "Portrait ";
            }

            // DisplayOrientation::Default means "the game does not care". Remove CNA's previous
            // value completely so SDL falls back to the bundle/platform set instead of retaining
            // an explicitly empty hint value.
            if (orientations == DisplayOrientation::Default)
            {
                SDL_ResetHint(SDL_HINT_ORIENTATIONS);
            }
            else
            {
                SDL_SetHint(SDL_HINT_ORIENTATIONS, hint.c_str());
            }

#if defined(CNA_PLATFORM_IOS)
            CNA::Internal::RequestAppleOrientationUpdate(window);
#else
            (void) window;
#endif
        }
    }

    GameWindow::GameWindow()
        : window_(nullptr),
          title_(),
          screenDeviceName_(),
          clientBounds_(),
          currentOrientation_(DisplayOrientation::Default),
          supportedOrientations_(
              DisplayOrientation::LandscapeLeft |
              DisplayOrientation::LandscapeRight |
              DisplayOrientation::Portrait
          ),
          allowUserResizing_(false),
          isBorderless_(false),
          pendingFullScreen_(false),
          hasPendingScreenDeviceChange_(false)
    {
    }

    GameWindow::GameWindow(SDL_Window* window)
        : GameWindow()
    {
        setWindowInternal(window);
    }

    bool GameWindow::getAllowUserResizingProperty() const
    {
        if (window_ != nullptr)
        {
            return (SDL_GetWindowFlags(window_) & SDL_WINDOW_RESIZABLE) != 0;
        }

        return allowUserResizing_;
    }

    void GameWindow::setAllowUserResizingProperty(bool value)
    {
        allowUserResizing_ = value;

        if (window_ != nullptr)
        {
            if (!SDL_SetWindowResizable(window_, value))
            {
                throw makeSdlError("SDL_SetWindowResizable");
            }
        }
    }

    Rectangle GameWindow::getClientBoundsProperty() const
    {
        if (window_ != nullptr)
        {
            return queryClientBoundsFromSDL();
        }

        return clientBounds_;
    }

    DisplayOrientation GameWindow::getCurrentOrientationProperty() const
    {
        return currentOrientation_;
    }

    SharpRuntime::IntPtr GameWindow::getHandleProperty() const
    {
        return reinterpret_cast<SharpRuntime::IntPtr>(window_);
    }

    SDL_Window* GameWindow::GetNativeSdlWindowEXT() const
    {
        return window_;
    }

    const GameWindow::String& GameWindow::getScreenDeviceNameProperty() const
    {
        return screenDeviceName_;
    }

    const GameWindow::String& GameWindow::getTitleProperty() const
    {
        return title_;
    }

    void GameWindow::setTitleProperty(const String& title)
    {
        if (title_ != title)
        {
            SetTitle(title);
            title_ = title;
        }
    }

    bool GameWindow::getIsBorderlessEXTProperty() const
    {
        if (window_ != nullptr)
        {
            return (SDL_GetWindowFlags(window_) & SDL_WINDOW_BORDERLESS) != 0;
        }

        return isBorderless_;
    }

    void GameWindow::setIsBorderlessEXTProperty(bool value)
    {
        isBorderless_ = value;

        if (window_ != nullptr)
        {
            if (!SDL_SetWindowBordered(window_, !value))
            {
                throw makeSdlError("SDL_SetWindowBordered");
            }
        }
    }

    void GameWindow::MinimizeEXT()
    {
        if (window_ != nullptr)
        {
            if (!SDL_MinimizeWindow(window_))
            {
                throw makeSdlError("SDL_MinimizeWindow");
            }
        }
    }

    void GameWindow::RestoreEXT()
    {
        if (window_ != nullptr)
        {
            if (!SDL_RestoreWindow(window_))
            {
                throw makeSdlError("SDL_RestoreWindow");
            }
        }
    }

    void GameWindow::BeginScreenDeviceChange(bool willBeFullScreen)
    {
        pendingFullScreen_ = willBeFullScreen;
        hasPendingScreenDeviceChange_ = true;
    }

    void GameWindow::EndScreenDeviceChange(const String& screenDeviceName, intcs clientWidth, intcs clientHeight)
    {
        const Rectangle oldBounds = clientBounds_;
        const String oldScreenDeviceName = screenDeviceName_;

        if (window_ != nullptr)
        {
            if (clientWidth > 0 && clientHeight > 0)
            {
#ifndef __ANDROID__
                if (!SDL_SetWindowSize(window_, clientWidth, clientHeight))
                {
                    throw makeSdlError("SDL_SetWindowSize");
                }
#endif
            }

            if (hasPendingScreenDeviceChange_)
            {
                if (!SDL_SetWindowFullscreen(window_, pendingFullScreen_))
                {
                    throw makeSdlError("SDL_SetWindowFullscreen");
                }
            }
        }

        screenDeviceName_ = screenDeviceName;
        hasPendingScreenDeviceChange_ = false;

        refreshCachedSDLState(false);

        if (oldBounds.Width != clientBounds_.Width || oldBounds.Height != clientBounds_.Height)
        {
            OnClientSizeChanged();
        }

        if (oldScreenDeviceName != screenDeviceName_)
        {
            OnScreenDeviceNameChanged();
        }
    }

    void GameWindow::EndScreenDeviceChange(const String& screenDeviceName)
    {
        const Rectangle bounds = getClientBoundsProperty();
        EndScreenDeviceChange(screenDeviceName, bounds.Width, bounds.Height);
    }

    const std::string& GameWindow::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GameWindow";
        return typeName;
    }

    void GameWindow::OnActivated()
    {
    }

    void GameWindow::OnClientSizeChanged()
    {
        ClientSizeChanged.Raise(this, System::EventArgs::Empty);
    }

    void GameWindow::OnDeactivated()
    {
    }

    void GameWindow::OnOrientationChanged()
    {
        OrientationChanged.Raise(this, System::EventArgs::Empty);
    }

    void GameWindow::OnPaint()
    {
    }

    void GameWindow::OnScreenDeviceNameChanged()
    {
        ScreenDeviceNameChanged.Raise(this, System::EventArgs::Empty);
    }

    void GameWindow::SetSupportedOrientations(DisplayOrientation orientations)
    {
        supportedOrientations_ = orientations;
        applySupportedOrientationsHint(orientations, window_);

        if (!orientationIsSupported(currentOrientation_))
        {
            if (orientationIsSupported(DisplayOrientation::Portrait))
            {
                setCurrentOrientationProperty(DisplayOrientation::Portrait);
            }
            else if (orientationIsSupported(DisplayOrientation::LandscapeLeft))
            {
                setCurrentOrientationProperty(DisplayOrientation::LandscapeLeft);
            }
            else if (orientationIsSupported(DisplayOrientation::LandscapeRight))
            {
                setCurrentOrientationProperty(DisplayOrientation::LandscapeRight);
            }
            else
            {
                setCurrentOrientationProperty(DisplayOrientation::Default);
            }
        }
    }

    void GameWindow::SetTitle(const String& title)
    {
        if (window_ == nullptr)
        {
            return;
        }

        if (!SDL_SetWindowTitle(window_, title.c_str()))
        {
            throw makeSdlError("SDL_SetWindowTitle");
        }
    }

    void GameWindow::setWindowInternal(SDL_Window* window)
    {
        window_ = window;

        if (window_ != nullptr)
        {
            const char* nativeTitle = SDL_GetWindowTitle(window_);
            title_ = nativeTitle != nullptr ? String(nativeTitle) : String();
            allowUserResizing_ = (SDL_GetWindowFlags(window_) & SDL_WINDOW_RESIZABLE) != 0;
            isBorderless_ = (SDL_GetWindowFlags(window_) & SDL_WINDOW_BORDERLESS) != 0;
        }

        refreshCachedSDLState(false);
    }

    void GameWindow::setCurrentOrientationProperty(DisplayOrientation value)
    {
        if (currentOrientation_ != value)
        {
            currentOrientation_ = value;
            OnOrientationChanged();
        }
    }

    void GameWindow::updateFromSDL()
    {
        refreshCachedSDLState(true);
    }

    void GameWindow::refreshCachedSDLState(bool raiseEvents)
    {
        const Rectangle oldBounds = clientBounds_;
        const String oldScreenDeviceName = screenDeviceName_;
        const DisplayOrientation oldOrientation = currentOrientation_;

        clientBounds_ = queryClientBoundsFromSDL();

        const String queriedScreenDeviceName = queryScreenDeviceNameFromSDL();
        if (!queriedScreenDeviceName.empty())
        {
            screenDeviceName_ = queriedScreenDeviceName;
        }

        const DisplayOrientation newOrientation = orientationFromBounds(clientBounds_);
        if (orientationIsSupported(newOrientation))
        {
            currentOrientation_ = newOrientation;
        }

        if (!raiseEvents)
        {
            return;
        }

        if (oldBounds.Width != clientBounds_.Width || oldBounds.Height != clientBounds_.Height)
        {
            OnClientSizeChanged();
        }

        if (oldScreenDeviceName != screenDeviceName_)
        {
            OnScreenDeviceNameChanged();
        }

        if (oldOrientation != currentOrientation_)
        {
            OnOrientationChanged();
        }
    }

    Rectangle GameWindow::queryClientBoundsFromSDL() const
    {
        if (window_ == nullptr)
        {
            return clientBounds_;
        }

        int width = clientBounds_.Width;
        int height = clientBounds_.Height;

        // Emscripten: SDL3's video backend can report "not initialized" for the first one or two
        // event-loop ticks after a real SDL_CreateWindow() has already returned a valid window --
        // the browser-side canvas/context setup it depends on finishes asynchronously, strictly
        // after control has already returned to C++. This is a real, reproducible startup race
        // (discovered driving CNA's own Emscripten renderers in a real browser for the first time),
        // not a caller error: SDL_GetWindowSize failing here is always reached from an event-driven
        // refresh (a real SDL_EVENT_WINDOW_*_CHANGED the browser itself fired) or a plain property
        // read, neither of which is a context where "throw and unwind the whole game loop" is an
        // acceptable response to one transient platform hiccup. Falling back to the last-known
        // bounds this one tick -- exactly like the `window_ == nullptr` branch above already does --
        // means the next real resize event (or the next explicit query, once the race has resolved)
        // still produces the correct size; nothing is permanently lost.
        if (!SDL_GetWindowSize(window_, &width, &height))
        {
            return clientBounds_;
        }

        return Rectangle(0, 0, width, height);
    }

    GameWindow::String GameWindow::queryScreenDeviceNameFromSDL() const
    {
        if (window_ == nullptr)
        {
            return screenDeviceName_;
        }

        const SDL_DisplayID displayId = SDL_GetDisplayForWindow(window_);
        if (displayId == 0)
        {
            return screenDeviceName_;
        }

        const char* displayName = SDL_GetDisplayName(displayId);
        return displayName != nullptr ? String(displayName) : String();
    }

    DisplayOrientation GameWindow::orientationFromBounds(const Rectangle& bounds) const
    {
        if (bounds.Width <= 0 || bounds.Height <= 0)
        {
            return DisplayOrientation::Default;
        }

        if (bounds.Height > bounds.Width)
        {
            return DisplayOrientation::Portrait;
        }

        return DisplayOrientation::LandscapeLeft;
    }

    bool GameWindow::orientationIsSupported(DisplayOrientation orientation) const
    {
        if (orientation == DisplayOrientation::Default)
        {
            return true;
        }

        return hasFlag(supportedOrientations_, orientation);
    }
} // namespace Microsoft::Xna::Framework
