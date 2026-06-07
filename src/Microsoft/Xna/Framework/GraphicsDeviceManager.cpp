#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework
{
    namespace
    {
        std::runtime_error makeSdlError(const char* operation)
        {
            return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
        }
    }

    GraphicsDeviceManager::GraphicsDeviceManager()
        : game_(nullptr),
          graphicsDevice_(nullptr),
          ownsGraphicsDevice_(false),
          drawBegun_(false),
          disposed_(false),
          prefsChanged_(true),
          supportsOrientations_(true),
          useResizedBackBuffer_(false),
          resizedBackBufferWidth_(0),
          resizedBackBufferHeight_(0),
          graphicsProfile_(Graphics::GraphicsProfile::Reach),
          isFullScreen_(false),
          preferMultiSampling_(false),
          preferredBackBufferFormat_(Graphics::SurfaceFormat::Color),
          preferredBackBufferHeight_(DefaultBackBufferHeight),
          preferredBackBufferWidth_(DefaultBackBufferWidth),
          preferredDepthStencilFormat_(Graphics::DepthFormat::Depth24),
          synchronizeWithVerticalRetrace_(true),
          supportedOrientations_(DisplayOrientation::Default),
          preferredPresentationMode_(PresentationMode::FixedHeightDynamicWidth)
    {
    }

    GraphicsDeviceManager::GraphicsDeviceManager(Game* game)
        : GraphicsDeviceManager()
    {
        if (game == nullptr)
        {
            throw std::invalid_argument("The game cannot be null.");
        }

        game_ = game;
        graphicsDevice_ = &game_->getGraphicsDeviceProperty();

        registerServices();

        // Match the old CNA behavior: give the backend a valid initial logical size.
        ApplyChanges();
    }

    GraphicsDeviceManager::~GraphicsDeviceManager()
    {
        Dispose(false);
    }

    Graphics::GraphicsProfile GraphicsDeviceManager::getGraphicsProfileProperty() const
    {
        return graphicsProfile_;
    }

    void GraphicsDeviceManager::setGraphicsProfileProperty(Graphics::GraphicsProfile value)
    {
        graphicsProfile_ = value;
        markPreferencesChanged();
    }

    Graphics::GraphicsDevice* GraphicsDeviceManager::getGraphicsDeviceProperty() const
    {
        return graphicsDevice_;
    }

    bool GraphicsDeviceManager::getIsFullScreenProperty() const
    {
        return isFullScreen_;
    }

    void GraphicsDeviceManager::setIsFullScreenProperty(bool value)
    {
        isFullScreen_ = value;
        markPreferencesChanged();
    }

    bool GraphicsDeviceManager::getPreferMultiSamplingProperty() const
    {
        return preferMultiSampling_;
    }

    void GraphicsDeviceManager::setPreferMultiSamplingProperty(bool value)
    {
        preferMultiSampling_ = value;
        markPreferencesChanged();
    }

    Graphics::SurfaceFormat GraphicsDeviceManager::getPreferredBackBufferFormatProperty() const
    {
        return preferredBackBufferFormat_;
    }

    void GraphicsDeviceManager::setPreferredBackBufferFormatProperty(Graphics::SurfaceFormat value)
    {
        preferredBackBufferFormat_ = value;
        markPreferencesChanged();
    }

    SharpRuntime::intcs GraphicsDeviceManager::getPreferredBackBufferHeightProperty() const
    {
        return preferredBackBufferHeight_;
    }

    void GraphicsDeviceManager::setPreferredBackBufferHeightProperty(SharpRuntime::intcs value)
    {
        preferredBackBufferHeight_ = value;
        markPreferencesChanged();
    }

    SharpRuntime::intcs GraphicsDeviceManager::getPreferredBackBufferWidthProperty() const
    {
        return preferredBackBufferWidth_;
    }

    void GraphicsDeviceManager::setPreferredBackBufferWidthProperty(SharpRuntime::intcs value)
    {
        preferredBackBufferWidth_ = value;
        markPreferencesChanged();
    }

    Graphics::DepthFormat GraphicsDeviceManager::getPreferredDepthStencilFormatProperty() const
    {
        return preferredDepthStencilFormat_;
    }

    void GraphicsDeviceManager::setPreferredDepthStencilFormatProperty(Graphics::DepthFormat value)
    {
        preferredDepthStencilFormat_ = value;
        markPreferencesChanged();
    }

    bool GraphicsDeviceManager::getSynchronizeWithVerticalRetraceProperty() const
    {
        return synchronizeWithVerticalRetrace_;
    }

    void GraphicsDeviceManager::setSynchronizeWithVerticalRetraceProperty(bool value)
    {
        synchronizeWithVerticalRetrace_ = value;
        markPreferencesChanged();
    }

    DisplayOrientation GraphicsDeviceManager::getSupportedOrientationsProperty() const
    {
        return supportedOrientations_;
    }

    void GraphicsDeviceManager::setSupportedOrientationsProperty(DisplayOrientation value)
    {
        supportedOrientations_ = value;
        markPreferencesChanged();
    }

    PresentationMode GraphicsDeviceManager::getPreferredPresentationModeProperty() const
    {
        return preferredPresentationMode_;
    }

    void GraphicsDeviceManager::setPreferredPresentationModeProperty(PresentationMode value)
    {
        preferredPresentationMode_ = value;
        markPreferencesChanged();
    }

    void GraphicsDeviceManager::ApplyChanges()
    {
        if (graphicsDevice_ == nullptr)
        {
            CreateDevice();
            return;
        }

        if (!prefsChanged_ && !useResizedBackBuffer_)
        {
            return;
        }

        GraphicsDeviceInformation gdi;
        gdi.setAdapterProperty(&graphicsDevice_->getAdapterProperty());
        gdi.setPresentationParametersProperty(graphicsDevice_->getPresentationParametersProperty().Clone());
        INTERNAL_CreateGraphicsDeviceInformation(gdi);

        if (game_ != nullptr)
        {
            if (supportsOrientations_)
            {
                game_->getWindowProperty().SetSupportedOrientations(supportedOrientations_);
            }

            auto& parameters = gdi.getPresentationParametersProperty();
            game_->getWindowProperty().BeginScreenDeviceChange(parameters.getIsFullScreenProperty());
            game_->getWindowProperty().EndScreenDeviceChange(
                gdi.getAdapterProperty() != nullptr ? gdi.getAdapterProperty()->getDeviceNameProperty() : std::string(),
                parameters.getBackBufferWidthProperty(),
                parameters.getBackBufferHeightProperty()
            );
        }

        OnDeviceResetting(this, System::EventArgs::Empty);

        applyToExistingBackend(gdi);

        OnDeviceReset(this, System::EventArgs::Empty);
        prefsChanged_ = false;
    }

    void GraphicsDeviceManager::ToggleFullScreen()
    {
        setIsFullScreenProperty(!getIsFullScreenProperty());
        ApplyChanges();
    }

    void GraphicsDeviceManager::Dispose()
    {
        Dispose(true);
    }

    void GraphicsDeviceManager::CreateDevice()
    {
        if (graphicsDevice_ != nullptr && ownsGraphicsDevice_)
        {
            OnDeviceDisposing(this, System::EventArgs::Empty);
            graphicsDevice_->Dispose();
            delete graphicsDevice_;
            graphicsDevice_ = nullptr;
            ownsGraphicsDevice_ = false;
        }

        if (game_ != nullptr)
        {
            graphicsDevice_ = &game_->getGraphicsDeviceProperty();
            ownsGraphicsDevice_ = false;
        }

        if (graphicsDevice_ == nullptr)
        {
            throw std::runtime_error(
                "GraphicsDeviceManager cannot create a GraphicsDevice without a Game-owned device in this CNA backend.");
        }

        GraphicsDeviceInformation gdi;
        gdi.setAdapterProperty(&Graphics::GraphicsAdapter::DefaultAdapter);
        Graphics::PresentationParameters parameters;
        if (game_ != nullptr)
        {
            parameters.setDeviceWindowHandleProperty(game_->getWindowProperty().getHandleProperty());
        }
        gdi.setPresentationParametersProperty(parameters);
        INTERNAL_CreateGraphicsDeviceInformation(gdi);

        if (game_ != nullptr)
        {
            auto& pp = gdi.getPresentationParametersProperty();

            if (supportsOrientations_)
            {
                game_->getWindowProperty().SetSupportedOrientations(supportedOrientations_);
            }

            game_->getWindowProperty().BeginScreenDeviceChange(pp.getIsFullScreenProperty());
            game_->getWindowProperty().EndScreenDeviceChange(
                gdi.getAdapterProperty() != nullptr ? gdi.getAdapterProperty()->getDeviceNameProperty() : std::string(),
                pp.getBackBufferWidthProperty(),
                pp.getBackBufferHeightProperty()
            );
        }

        applyToExistingBackend(gdi);
        OnDeviceCreated(this, System::EventArgs::Empty);
        prefsChanged_ = false;
    }

    bool GraphicsDeviceManager::BeginDraw()
    {
        if (graphicsDevice_ == nullptr)
        {
            return false;
        }

        drawBegun_ = true;
        return true;
    }

    void GraphicsDeviceManager::EndDraw()
    {
        if (graphicsDevice_ != nullptr && drawBegun_)
        {
            drawBegun_ = false;
            graphicsDevice_->Present();
        }
    }

    const std::string& GraphicsDeviceManager::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GraphicsDeviceManager";
        return typeName;
    }

    void GraphicsDeviceManager::Dispose(bool disposing)
    {
        if (disposed_)
        {
            return;
        }

        unregisterServices();

        if (disposing && graphicsDevice_ != nullptr && ownsGraphicsDevice_)
        {
            OnDeviceDisposing(this, System::EventArgs::Empty);
            graphicsDevice_->Dispose();
            delete graphicsDevice_;
            graphicsDevice_ = nullptr;
            ownsGraphicsDevice_ = false;
        }

        Disposed.Raise(this, System::EventArgs::Empty);
        disposed_ = true;
    }

    System::EventHandler<System::EventArgs>& GraphicsDeviceManager::getDeviceCreatedEvent()
    {
        return DeviceCreated;
    }

    System::EventHandler<System::EventArgs>& GraphicsDeviceManager::getDeviceDisposingEvent()
    {
        return DeviceDisposing;
    }

    System::EventHandler<System::EventArgs>& GraphicsDeviceManager::getDeviceResetEvent()
    {
        return DeviceReset;
    }

    System::EventHandler<System::EventArgs>& GraphicsDeviceManager::getDeviceResettingEvent()
    {
        return DeviceResetting;
    }

    void GraphicsDeviceManager::OnDeviceCreated(System::Object* sender, const System::EventArgs& args)
    {
        DeviceCreated.Raise(sender, args);
    }

    void GraphicsDeviceManager::OnDeviceDisposing(System::Object* sender, const System::EventArgs& args)
    {
        DeviceDisposing.Raise(sender, args);
    }

    void GraphicsDeviceManager::OnDeviceReset(System::Object* sender, const System::EventArgs& args)
    {
        DeviceReset.Raise(sender, args);
    }

    void GraphicsDeviceManager::OnDeviceResetting(System::Object* sender, const System::EventArgs& args)
    {
        DeviceResetting.Raise(sender, args);
    }

    void GraphicsDeviceManager::OnPreparingDeviceSettings(System::Object* sender,
                                                          PreparingDeviceSettingsEventArgs& args)
    {
        PreparingDeviceSettings.Raise(sender, args);
    }

    bool GraphicsDeviceManager::CanResetDevice(const GraphicsDeviceInformation& newDeviceInfo)
    {
        (void)newDeviceInfo;
        return graphicsDevice_ != nullptr;
    }

    GraphicsDeviceInformation GraphicsDeviceManager::FindBestDevice(bool anySuitableDevice)
    {
        (void)anySuitableDevice;

        GraphicsDeviceInformation gdi;
        gdi.setAdapterProperty(&Graphics::GraphicsAdapter::DefaultAdapter);
        gdi.setPresentationParametersProperty(Graphics::PresentationParameters());
        INTERNAL_CreateGraphicsDeviceInformation(gdi);
        return gdi;
    }

    void GraphicsDeviceManager::RankDevices(std::vector<GraphicsDeviceInformation>& foundDevices)
    {
        (void)foundDevices;
    }

    void GraphicsDeviceManager::INTERNAL_OnClientSizeChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)args;

        auto* window = dynamic_cast<GameWindow*>(sender);
        if (window == nullptr)
        {
            return;
        }

        Rectangle size = window->getClientBoundsProperty();
        resizedBackBufferWidth_ = size.Width;
        resizedBackBufferHeight_ = size.Height;
        useResizedBackBuffer_ = true;
        ApplyChanges();
    }

    void GraphicsDeviceManager::INTERNAL_CreateGraphicsDeviceInformation(GraphicsDeviceInformation& gdi)
    {
        auto& pp = gdi.getPresentationParametersProperty();

        if (useResizedBackBuffer_)
        {
            pp.setBackBufferWidthProperty(resizedBackBufferWidth_);
            pp.setBackBufferHeightProperty(resizedBackBufferHeight_);
            useResizedBackBuffer_ = false;
        }
        else
        {
            if (!supportsOrientations_)
            {
                pp.setBackBufferWidthProperty(preferredBackBufferWidth_);
                pp.setBackBufferHeightProperty(preferredBackBufferHeight_);
            }
            else
            {
                const SharpRuntime::intcs minSize = std::min(preferredBackBufferWidth_, preferredBackBufferHeight_);
                const SharpRuntime::intcs maxSize = std::max(preferredBackBufferWidth_, preferredBackBufferHeight_);

                if (pp.getDisplayOrientationProperty() == DisplayOrientation::Portrait)
                {
                    pp.setBackBufferWidthProperty(minSize);
                    pp.setBackBufferHeightProperty(maxSize);
                }
                else
                {
                    pp.setBackBufferWidthProperty(maxSize);
                    pp.setBackBufferHeightProperty(minSize);
                }
            }
        }

        pp.setBackBufferFormatProperty(preferredBackBufferFormat_);
        pp.setDepthStencilFormatProperty(preferredDepthStencilFormat_);
        pp.setIsFullScreenProperty(isFullScreen_);
        pp.setPresentationIntervalProperty(
            synchronizeWithVerticalRetrace_ ? Graphics::PresentInterval::One : Graphics::PresentInterval::Immediate
        );

        if (!preferMultiSampling_)
        {
            pp.setMultiSampleCountProperty(0);
        }
        else if (pp.getMultiSampleCountProperty() == 0)
        {
            pp.setMultiSampleCountProperty(8);
        }

        gdi.setGraphicsProfileProperty(graphicsProfile_);

        PreparingDeviceSettingsEventArgs args(gdi);
        OnPreparingDeviceSettings(this, args);
    }

    void GraphicsDeviceManager::markPreferencesChanged()
    {
        prefsChanged_ = true;
    }

    void GraphicsDeviceManager::registerServices()
    {
        // TODO: register IGraphicsDeviceManager and IGraphicsDeviceService once
        // Game::getServicesProperty() is available in CNA.

        // Wire window resize → viewport update.
        game_->getWindowProperty().ClientSizeChanged +=
            [this](System::Object* sender, const System::EventArgs& args)
            {
                INTERNAL_OnClientSizeChanged(sender, args);
            };
    }

    void GraphicsDeviceManager::unregisterServices()
    {
        // TODO: unregister services once Game::getServicesProperty() is available in CNA.
    }

    SDL_Window* GraphicsDeviceManager::tryGetSDLWindow() const
    {
        if (graphicsDevice_ == nullptr)
        {
            return nullptr;
        }

        return graphicsDevice_->GetWindowInternal();
    }

    void GraphicsDeviceManager::applyToExistingBackend(GraphicsDeviceInformation& gdi)
    {
        if (graphicsDevice_ == nullptr)
        {
            return;
        }

        auto& pp = gdi.getPresentationParametersProperty();

        SDL_Window* window = tryGetSDLWindow();
        if (window != nullptr)
        {
            if (!SDL_SetWindowFullscreen(window, pp.getIsFullScreenProperty()))
            {
                throw makeSdlError("SDL_SetWindowFullscreen");
            }

            if (pp.getBackBufferWidthProperty() > 0 && pp.getBackBufferHeightProperty() > 0)
            {
#ifndef __ANDROID__
                if (!SDL_SetWindowSize(window, pp.getBackBufferWidthProperty(), pp.getBackBufferHeightProperty()))
                {
                    throw makeSdlError("SDL_SetWindowSize");
                }
#endif
            }
        }

        graphicsDevice_->SetPresentationMode(static_cast<int>(preferredPresentationMode_));
        graphicsDevice_->SetVirtualResolution(pp.getBackBufferWidthProperty(), pp.getBackBufferHeightProperty());

        // Full XNA-compatible device reset belongs to GraphicsDevice::Reset once the
        // GraphicsDevice/PresentationParameters backend is fully ported.
        // graphicsDevice_->Reset(pp, *gdi.getAdapterProperty());

        graphicsDevice_->UpdateViewportFromWindow();
    }
}
