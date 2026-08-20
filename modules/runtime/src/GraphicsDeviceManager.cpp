// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

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
        // Matches FNA's SupportsOrientations: only iOS and Android
        // care about device orientation. On desktop platforms the back buffer keeps
        // the requested PreferredBackBufferWidth x Height verbatim (no landscape swap),
        // which is the XNA 4.0 / FNA desktop behaviour.
        bool platformSupportsOrientations()
        {
            // The ambient platform rather than the game's own: the default constructor has no
            // game to ask, and this is a property of the process, not of a particular game.
            const std::string name =
                CNA::Platform::GetCurrentPlatform().GetSystemInfo()->GetPlatformName();
            return name == "iOS" || name == "Android";
        }
    }

    GraphicsDeviceManager::GraphicsDeviceManager()
        : game_(nullptr),
          graphicsDevice_(nullptr),
          ownsGraphicsDevice_(false),
          deviceEventsSubscribed_(false),
          drawBegun_(false),
          disposed_(false),
          prefsChanged_(true),
          supportsOrientations_(platformSupportsOrientations()),
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

        // Deliberately NOT calling ApplyChanges() here (cna-template/missing.md): Game's own
        // device already exists with sane defaults at this point (CNA's Game always pre-owns its
        // GraphicsDevice, unlike real FNA, where GraphicsDeviceManager creates it), and
        // Game::DoInitialize() unconditionally calls CreateDevice() shortly after this
        // constructor returns -- so calling ApplyChanges() here just did the exact same renderer
        // reconfiguration (SetPresentationMode + Reset) twice in a row for every Game that owns a
        // GraphicsDeviceManager, causing a visible double-reconfiguration flicker on startup.
        // Matches FNA's own explicit guidance (GraphicsDeviceManager.cs, ApplyChanges()): "Note
        // that if you hit this block, it's probably because you called ApplyChanges in the
        // constructor. The device created here gets destroyed and recreated by the game, so maybe
        // don't do that!" -- Game::DoInitialize()'s own CreateDevice() call is the single source
        // of truth for first-time setup.
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

        // REMED-CORE-007: applyToExistingRenderer() below calls GraphicsDevice::Reset(), which
        // raises the device's own DeviceResetting/DeviceReset. CreateDevice() subscribes this
        // manager to those events and forwards them via OnDeviceResetting()/OnDeviceReset(), so
        // this method must not raise them a second time itself (matches FNA's own ApplyChanges(),
        // which never calls OnDeviceResetting/OnDeviceReset directly either).
        applyToExistingRenderer(gdi);

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
            deviceEventsSubscribed_ = false;
        }

        if (game_ != nullptr)
        {
            graphicsDevice_ = &game_->getGraphicsDeviceProperty();
            ownsGraphicsDevice_ = false;
        }

        if (graphicsDevice_ == nullptr)
        {
            throw std::runtime_error(
                "GraphicsDeviceManager cannot create a GraphicsDevice without a Game-owned device in this CNA renderer.");
        }

        GraphicsDeviceInformation gdi;
        gdi.setAdapterProperty(&Graphics::GraphicsAdapter::getDefaultAdapterProperty());
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

        applyToExistingRenderer(gdi);

        // REMED-CORE-007: FNA's IGraphicsDeviceManager.CreateDevice() wires
        // graphicsDevice.DeviceResetting += OnDeviceResetting; graphicsDevice.DeviceReset +=
        // OnDeviceReset; so a real renderer-detected device-lost/reset (raised directly on
        // GraphicsDevice's own events, e.g. by GraphicsDevice's deviceEventCallback) reaches this
        // manager's own listeners, not just resets this manager itself initiates. Installed after
        // applyToExistingRenderer()'s own settle-in Reset() call above, so that initial device
        // creation raises only DeviceCreated below, matching FNA (a freshly-constructed FNA
        // GraphicsDevice never raises DeviceResetting/DeviceReset). Guarded so a second
        // CreateDevice() call against the same still-live device (game_-attached case; CreateDevice()
        // is public API) does not accumulate duplicate subscriptions and double-forward every event.
        if (!deviceEventsSubscribed_)
        {
            graphicsDevice_->DeviceResetting +=
                [this](System::Object* sender, const System::EventArgs& args) { OnDeviceResetting(sender, args); };
            graphicsDevice_->DeviceReset +=
                [this](System::Object* sender, const System::EventArgs& args) { OnDeviceReset(sender, args); };
            deviceEventsSubscribed_ = true;
        }

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

        if (disposing && graphicsDevice_ != nullptr)
        {
            // REMED-CORE-014: this raise must not be gated on ownsGraphicsDevice_. CNA's Game
            // always pre-owns its GraphicsDevice_ (unlike FNA, where GraphicsDeviceManager always
            // owns the device it manages), so ownsGraphicsDevice_ is correctly false for every
            // Game-attached manager -- gating the raise on it left DeviceDisposing permanently
            // dead for the one configuration this whole codebase actually constructs, which in
            // turn left REMED-CORE-006's Game::Initialize() subscription with nothing to ever
            // invoke. Only the actual delete below stays ownsGraphicsDevice_-gated: graphicsDevice_
            // may point at a Game-owned value member, and deleting a non-heap object is memory
            // corruption.
            OnDeviceDisposing(this, System::EventArgs::Empty);

            if (ownsGraphicsDevice_)
            {
                graphicsDevice_->Dispose();
                delete graphicsDevice_;
                ownsGraphicsDevice_ = false;
            }

            graphicsDevice_ = nullptr;
            deviceEventsSubscribed_ = false;
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
        // FNA's own OnDeviceDisposing/OnDeviceReset/OnDeviceResetting all re-send with `this`
        // rather than forwarding the passed-in sender (unlike OnDeviceCreated, which does forward
        // it) -- an asymmetry in the real FNA source, preserved here rather than "cleaned up".
        // Previously invisible in CNA because every prior call site already passed `this`; now
        // observable once REMED-CORE-007's device-event forwarding calls these with the managed
        // GraphicsDevice as sender.
        (void)sender;
        DeviceDisposing.Raise(this, args);
    }

    void GraphicsDeviceManager::OnDeviceReset(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        DeviceReset.Raise(this, args);
    }

    void GraphicsDeviceManager::OnDeviceResetting(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        DeviceResetting.Raise(this, args);
    }

    void GraphicsDeviceManager::OnPreparingDeviceSettings(System::Object* sender,
                                                          PreparingDeviceSettingsEventArgs& args)
    {
        PreparingDeviceSettings.Raise(sender, args);
    }

    bool GraphicsDeviceManager::CanResetDevice(const GraphicsDeviceInformation& newDeviceInfo)
    {
        // FNA throws NotImplementedException; CNA returns true when a device exists.
        (void)newDeviceInfo;
        return graphicsDevice_ != nullptr;
    }

    GraphicsDeviceInformation GraphicsDeviceManager::FindBestDevice(bool anySuitableDevice)
    {
        // FNA throws NotImplementedException; CNA returns a sensible default GDI.
        (void)anySuitableDevice;

        GraphicsDeviceInformation gdi;
        gdi.setAdapterProperty(&Graphics::GraphicsAdapter::getDefaultAdapterProperty());
        gdi.setPresentationParametersProperty(Graphics::PresentationParameters());
        INTERNAL_CreateGraphicsDeviceInformation(gdi);
        return gdi;
    }

    void GraphicsDeviceManager::RankDevices(std::vector<GraphicsDeviceInformation>& foundDevices)
    {
        // FNA throws NotImplementedException; CNA is a no-op (single adapter, no ranking needed).
        (void)foundDevices;
    }

    void GraphicsDeviceManager::INTERNAL_OnClientSizeChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)args;
        (void)sender;

        // Do NOT call ApplyChanges() here. ApplyChanges() would forward the
        // new physical window size as the virtual resolution, corrupting the
        // game's logical coordinate space and breaking scaling on all renderers.
        // Each renderer queries the current window size dynamically every frame, so
        // no explicit notification is required — only the XNA Viewport needs
        // to be refreshed so scissor/viewport state stays coherent.
        if (graphicsDevice_ != nullptr)
            graphicsDevice_->UpdateViewportFromWindow();
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
            // FNA queries FNA3D_GetMaxMultiSampleCount and caps at 8; CNA uses 8 directly (no FNA3D).
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
        if (game_->getServicesProperty().GetService<IGraphicsDeviceManager>() != nullptr)
        {
            throw std::invalid_argument(
                "A GraphicsDeviceManager is already registered with this Game.");
        }

        game_->getServicesProperty().AddService<IGraphicsDeviceManager>(this);
        game_->getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(this);

        // Wire window resize → viewport update.
        game_->getWindowProperty().ClientSizeChanged +=
            [this](System::Object* sender, const System::EventArgs& args)
            {
                INTERNAL_OnClientSizeChanged(sender, args);
            };
    }

    void GraphicsDeviceManager::unregisterServices()
    {
        if (game_ == nullptr)
        {
            return;
        }

        game_->getServicesProperty().RemoveService<IGraphicsDeviceManager>();
        game_->getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
    }

    void GraphicsDeviceManager::applyToExistingRenderer(GraphicsDeviceInformation& gdi)
    {
        if (graphicsDevice_ == nullptr)
        {
            return;
        }

        auto& pp = gdi.getPresentationParametersProperty();

        // plans/plan_dx9.md D9-103 finding: Game's own GraphicsDevice_ member is eagerly
        // default-constructed (hardcoded GraphicsProfile::Reach) before GraphicsDeviceManager
        // even exists, so a game's `graphics.GraphicsProfile = GraphicsProfile.HiDef;
        // graphics.ApplyChanges();` request had no path to ever reach the real device -- fixed by
        // propagating gdi's own profile (set from graphicsProfile_ just above, in
        // PrepareDeviceSettings) into the existing device explicitly, here, before Reset().
        graphicsDevice_->SetGraphicsProfileEXT(gdi.getGraphicsProfileProperty());

        // The presentation/scaling mode must be applied before Reset()'s own
        // SetVirtualResolution() call below -- the renderer's logical-presentation size
        // computation depends on which mode is already active, so setting the mode
        // afterward can leave a stale logical size computed under the previous mode.
        graphicsDevice_->SetPresentationMode(static_cast<int>(preferredPresentationMode_));

        // Task 902: real in-place device reset -- stores PP, applies fullscreen/window size
        // (non-fatal on fullscreen failure, see applyPresentationParametersToWindow()),
        // updates virtual resolution, and reconfigures renderer-construction-time-only
        // properties like MultiSampleCount (GraphicsDeviceManager.PreferMultiSampling) via
        // IGraphicsRenderer::ApplyMultiSampleCount(), writing the real applied value back into
        // the stored PresentationParameters. Also raises GraphicsDevice's own
        // DeviceResetting/DeviceReset events (separate from this class's own, raised by the
        // ApplyChanges()/CreateDevice() callers of this method).
        graphicsDevice_->Reset(pp, *gdi.getAdapterProperty());

        graphicsDevice_->UpdateViewportFromWindow();
    }
}
