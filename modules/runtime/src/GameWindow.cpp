// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GameWindow.hpp"

#include "CNA/Platform/IPlatformWindow.hpp"

namespace Microsoft::Xna::Framework
{
    namespace
    {
        bool hasFlag(DisplayOrientation value, DisplayOrientation flag)
        {
            return (static_cast<int>(value) & static_cast<int>(flag)) != 0;
        }

        // plan_apple.md APPLE-15. On a desktop the supported-orientation set is the framework's
        // own bookkeeping, but on iOS and Android the operating system decides whether the device
        // may rotate at all, and it only learns the answer if the platform layer is told. Without
        // forwarding it, XNA's SupportedOrientations would silently mean nothing on the one
        // platform family it was designed for.
        //
        // DisplayOrientation::Default means "the game does not care", which maps to the platform's
        // own no-preference value rather than to an empty set.
        CNA::Platform::ScreenOrientation toPlatformOrientations(DisplayOrientation orientations)
        {
            if (orientations == DisplayOrientation::Default)
            {
                return CNA::Platform::ScreenOrientation::None;
            }

            CNA::Platform::ScreenOrientation accepted = CNA::Platform::ScreenOrientation::None;
            if (hasFlag(orientations, DisplayOrientation::LandscapeLeft))
            {
                accepted = accepted | CNA::Platform::ScreenOrientation::LandscapeLeft;
            }
            if (hasFlag(orientations, DisplayOrientation::LandscapeRight))
            {
                accepted = accepted | CNA::Platform::ScreenOrientation::LandscapeRight;
            }
            if (hasFlag(orientations, DisplayOrientation::Portrait))
            {
                accepted = accepted | CNA::Platform::ScreenOrientation::Portrait;
            }
            return accepted;
        }
    }

    GameWindow::GameWindow()
        : window_(nullptr),
          legacyHandle_(0),
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

    GameWindow::GameWindow(CNA::Platform::IPlatformWindow* window)
        : GameWindow()
    {
        setWindowInternal(window, 0);
    }

    GameWindow::~GameWindow() = default;

    bool GameWindow::getAllowUserResizingProperty() const
    {
        if (window_ != nullptr)
        {
            return window_->IsResizable();
        }

        return allowUserResizing_;
    }

    void GameWindow::setAllowUserResizingProperty(bool value)
    {
        allowUserResizing_ = value;

        if (window_ != nullptr)
        {
            window_->SetResizable(value);
        }
    }

    Rectangle GameWindow::getClientBoundsProperty() const
    {
        if (window_ != nullptr)
        {
            return queryClientBoundsFromPlatform();
        }

        return clientBounds_;
    }

    DisplayOrientation GameWindow::getCurrentOrientationProperty() const
    {
        return currentOrientation_;
    }

    SharpRuntime::IntPtr GameWindow::getHandleProperty() const
    {
        return legacyHandle_;
    }

    CNA::Platform::NativeWindowHandle GameWindow::GetNativeWindowHandleEXT() const
    {
        return window_ != nullptr
            ? window_->GetNativeHandle()
            : CNA::Platform::NativeWindowHandle{};
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
            return window_->IsBorderless();
        }

        return isBorderless_;
    }

    void GameWindow::setIsBorderlessEXTProperty(bool value)
    {
        isBorderless_ = value;

        if (window_ != nullptr)
        {
            window_->SetBorderless(value);
        }
    }

    void GameWindow::MinimizeEXT()
    {
        if (window_ != nullptr)
        {
            window_->Minimize();
        }
    }

    void GameWindow::RestoreEXT()
    {
        if (window_ != nullptr)
        {
            window_->Restore();
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
                window_->SetSize(clientWidth, clientHeight);
                window_->Sync();
#endif
            }

            if (hasPendingScreenDeviceChange_)
            {
                window_->SetFullscreenMode(
                    pendingFullScreen_
                        ? CNA::Platform::WindowFullscreenMode::ExclusiveFullscreen
                        : CNA::Platform::WindowFullscreenMode::Windowed);
            }
        }

        screenDeviceName_ = screenDeviceName;
        hasPendingScreenDeviceChange_ = false;

        refreshCachedPlatformState(false);

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
        if (window_ != nullptr)
        {
            window_->SetSupportedOrientations(toPlatformOrientations(orientations));
        }

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

        window_->SetTitle(title);
    }

    void GameWindow::setWindowInternal(CNA::Platform::IPlatformWindow* window,
                                       const SharpRuntime::IntPtr legacyHandle)
    {
        window_ = window;
        legacyHandle_ = legacyHandle;

        if (window_ != nullptr)
        {
            title_ = window_->GetTitle();
            allowUserResizing_ = window_->IsResizable();
            isBorderless_ = window_->IsBorderless();
        }

        refreshCachedPlatformState(false);
    }

    CNA::Platform::IPlatformWindow* GameWindow::getPlatformWindowInternal() const
    {
        return window_;
    }

    void GameWindow::setCurrentOrientationProperty(DisplayOrientation value)
    {
        if (currentOrientation_ != value)
        {
            currentOrientation_ = value;
            OnOrientationChanged();
        }
    }

    void GameWindow::updateFromPlatform()
    {
        refreshCachedPlatformState(true);
    }

    void GameWindow::refreshCachedPlatformState(bool raiseEvents)
    {
        const Rectangle oldBounds = clientBounds_;
        const String oldScreenDeviceName = screenDeviceName_;
        const DisplayOrientation oldOrientation = currentOrientation_;

        clientBounds_ = queryClientBoundsFromPlatform();

        const String queriedScreenDeviceName = queryScreenDeviceNameFromPlatform();
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

    Rectangle GameWindow::queryClientBoundsFromPlatform() const
    {
        if (window_ == nullptr)
        {
            return clientBounds_;
        }

        const CNA::Platform::WindowBounds bounds = window_->GetClientBounds();
        // XNA's ClientBounds is client-local. The platform also carries desktop placement in the
        // same value for window managers that expose it; that position must not leak into XNA.
        return Rectangle(0, 0, bounds.width, bounds.height);
    }

    GameWindow::String GameWindow::queryScreenDeviceNameFromPlatform() const
    {
        if (window_ == nullptr)
        {
            return screenDeviceName_;
        }

        const std::string displayName = window_->GetDisplayName();
        return displayName.empty() ? screenDeviceName_ : String(displayName);
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
