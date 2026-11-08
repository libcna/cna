// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include "CNA/ProjectGraphicsProfile.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/OverflowException.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

namespace Microsoft::Xna::Framework
{
    namespace
    {
        int surfaceFormatBitDepth(Graphics::SurfaceFormat format)
        {
            // XNA GraphicsDeviceInformationComparer.SurfaceFormatBitDepth IL_0000-0024:
            // this is deliberately a small whitelist, not the storage size of every format.
            switch (format)
            {
            case Graphics::SurfaceFormat::Color:
            case Graphics::SurfaceFormat::Rgba1010102: return 32;
            case Graphics::SurfaceFormat::Bgr565:
            case Graphics::SurfaceFormat::Bgra5551:
            case Graphics::SurfaceFormat::Bgra4444: return 16;
            default: return 0;
            }
        }

        std::int32_t pixelArea(int width, int height)
        {
            return std::bit_cast<std::int32_t>(std::uint32_t(width) * std::uint32_t(height));
        }

        std::int32_t pixelAreaError(std::int32_t area, std::int32_t target)
        {
            // Compare IL_0212-023e uses unchecked Int32 mul/sub followed by Math.Abs(Int32).
            const auto difference = std::bit_cast<std::int32_t>(std::uint32_t(area) - std::uint32_t(target));
            if (difference == std::numeric_limits<std::int32_t>::min())
                throw System::OverflowException();
            return difference < 0 ? -difference : difference;
        }

        int compareDevices(const GraphicsDeviceManager& manager,
                           const GraphicsDeviceInformation& first,
                           const GraphicsDeviceInformation& second)
        {
            // XNA Compare IL_0000-0098: profile, fullscreen, format rank, then samples.
            if (first.getGraphicsProfileProperty() != second.getGraphicsProfileProperty())
                return first.getGraphicsProfileProperty() > second.getGraphicsProfileProperty() ? -1 : 1;
            const auto& a = first.getPresentationParametersProperty();
            const auto& b = second.getPresentationParametersProperty();
            if (a.getIsFullScreenProperty() != b.getIsFullScreenProperty())
                return a.getIsFullScreenProperty() == manager.getIsFullScreenProperty() ? -1 : 1;
            const auto rankFormat = [&](Graphics::SurfaceFormat format)
            {
                const auto preferred = manager.getPreferredBackBufferFormatProperty();
                if (format == preferred) return 0;
                return surfaceFormatBitDepth(format) == surfaceFormatBitDepth(preferred)
                    ? 1 : std::numeric_limits<int>::max();
            };
            const int rankA = rankFormat(a.getBackBufferFormatProperty());
            const int rankB = rankFormat(b.getBackBufferFormatProperty());
            if (rankA != rankB) return rankA < rankB ? -1 : 1;
            if (a.getMultiSampleCountProperty() != b.getMultiSampleCountProperty())
                return a.getMultiSampleCountProperty() > b.getMultiSampleCountProperty() ? -1 : 1;

            const int width = manager.getPreferredBackBufferWidthProperty();
            const int height = manager.getPreferredBackBufferHeightProperty();
            const bool useDefault = width == 0 || height == 0;
            const float aspect = useDefault
                ? float(GraphicsDeviceManager::DefaultBackBufferWidth) / GraphicsDeviceManager::DefaultBackBufferHeight
                : float(width) / float(height);
            const float errorA = std::abs(float(a.getBackBufferWidthProperty()) / float(a.getBackBufferHeightProperty()) - aspect);
            const float errorB = std::abs(float(b.getBackBufferWidthProperty()) / float(b.getBackBufferHeightProperty()) - aspect);
            // Compare IL_0099-0133: only differences strictly greater than 0.2f decide.
            // Unordered (NaN) differences fall through, as with IL's ble.un.
            if (std::abs(errorA - errorB) > 0.2f) return errorA < errorB ? -1 : 1;

            // Compare IL_0134-024f: fullscreen with unspecified size uses each adapter's
            // desktop area; windowed uses 800*480. Otherwise both use the requested area.
            const auto targetArea = [&](const GraphicsDeviceInformation& device)
            {
                if (!useDefault) return pixelArea(width, height);
                if (manager.getIsFullScreenProperty())
                {
                    const auto& mode = device.getAdapterProperty()->getCurrentDisplayModeProperty();
                    return pixelArea(mode.getWidthProperty(), mode.getHeightProperty());
                }
                return pixelArea(GraphicsDeviceManager::DefaultBackBufferWidth,
                                 GraphicsDeviceManager::DefaultBackBufferHeight);
            };
            const auto pixelsA = pixelAreaError(pixelArea(a.getBackBufferWidthProperty(), a.getBackBufferHeightProperty()), targetArea(first));
            const auto pixelsB = pixelAreaError(pixelArea(b.getBackBufferWidthProperty(), b.getBackBufferHeightProperty()), targetArea(second));
            if (pixelsA != pixelsB) return pixelsA < pixelsB ? -1 : 1;
            // Compare IL_0250-027d: no adapter ordinal or other presentation fields break ties.
            if (first.getAdapterProperty() != second.getAdapterProperty())
            {
                if (first.getAdapterProperty()->getIsDefaultAdapterProperty()) return -1;
                if (second.getAdapterProperty()->getIsDefaultAdapterProperty()) return 1;
            }
            return 0;
        }

        void sortDevices(const GraphicsDeviceManager& manager,
                         std::vector<GraphicsDeviceInformation>& devices,
                         std::ptrdiff_t left, std::ptrdiff_t right)
        {
            // XNA RankDevicesPlatform IL_0000-000c uses unstable List.Sort. Its pairwise
            // aspect tolerance is not a strict weak ordering, so std::sort is inapplicable.
            // Follow CLR 4.0.30319.1 ArraySortHelper<T>.QuickSort IL_0000-00c2:
            // median-of-three, two scanning indices, recurse on the smaller partition.
            while (left < right)
            {
                auto i = left;
                auto j = right;
                const auto middle = left + (right - left) / 2;
                const auto swapIfGreater = [&](std::ptrdiff_t a, std::ptrdiff_t b)
                {
                    if (a != b && compareDevices(manager, devices[a], devices[b]) > 0)
                        std::swap(devices[a], devices[b]);
                };
                swapIfGreater(i, middle);
                swapIfGreater(i, j);
                swapIfGreater(middle, j);
                const auto pivot = devices[middle];
                do
                {
                    while (i <= right && compareDevices(manager, devices[i], pivot) < 0) ++i;
                    while (j >= left && compareDevices(manager, pivot, devices[j]) < 0) --j;
                    if (i > right || j < left) throw System::IndexOutOfRangeException();
                    if (i <= j)
                    {
                        if (i != j) std::swap(devices[i], devices[j]);
                        ++i;
                        --j;
                    }
                } while (i <= j);
                if (j - left <= right - i)
                {
                    if (left < j) sortDevices(manager, devices, left, j);
                    left = i;
                }
                else
                {
                    if (i < right) sortDevices(manager, devices, i, right);
                    right = j;
                }
            }
        }

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
          deviceDisposingToken_(0),
          deviceResettingToken_(0),
          deviceResetToken_(0),
          drawBegun_(false),
          disposed_(false),
          prefsChanged_(true),
          supportsOrientations_(platformSupportsOrientations()),
          useResizedBackBuffer_(false),
          resizedBackBufferWidth_(0),
          resizedBackBufferHeight_(0),
          graphicsProfile_(CNA::GetProjectGraphicsProfileEXT()),
          isFullScreen_(false),
          preferMultiSampling_(false),
          preferredBackBufferFormat_(Graphics::SurfaceFormat::Color),
          preferredBackBufferHeight_(DefaultBackBufferHeight),
          preferredBackBufferWidth_(DefaultBackBufferWidth),
          preferredDepthStencilFormat_(Graphics::DepthFormat::Depth24),
          synchronizeWithVerticalRetrace_(true),
          supportedOrientations_(DisplayOrientation::Default),
          // Letterbox, because GraphicsDevice.Viewport must be the backbuffer. XNA and FNA
          // both report (0, 0, BackBufferWidth, BackBufferHeight) and scale the backbuffer
          // onto whatever shape the window is; FixedHeightDynamicWidth instead widens the
          // LOGICAL width to the window's aspect, so a game that anchors anything to
          // Viewport.Width lays it out somewhere the rest of its own frame does not reach.
          preferredPresentationMode_(PresentationMode::Letterbox)
    {
    }

    GraphicsDeviceManager::GraphicsDeviceManager(Game* game)
        : GraphicsDeviceManager()
    {
        // XNA GraphicsDeviceManager..ctor IL_002a-003c (Resources.GameCannotBeNull).
        if (game == nullptr)
        {
            throw System::ArgumentNullException("game", "Game cannot be null.");
        }

        // XNA ..ctor IL_0044-0065 refuses a second manager before IL_0066-00d2 register or hook
        // anything (Resources.GraphicsDeviceManagerAlreadyPresent). Refused before game_ is set:
        // this delegating constructor's exception runs the destructor, which detaches through
        // game_ and would otherwise unhook the registered manager's window handler.
        if (game->getServicesProperty().GetService<IGraphicsDeviceManager>() != nullptr)
        {
            throw System::ArgumentException(
                "A graphics device manager is already registered.  The graphics device manager "
                "cannot be changed once it is set.");
        }

        game_ = game;
        graphicsDevice_ = &game_->getGraphicsDeviceProperty();

        // Let the window decide orientation from the surface the game draws into rather than from
        // the window around it. Installed here rather than in ApplyChanges(), which returns early
        // whenever nothing changed and so cannot be relied on to run at all. Captured as a
        // provider, not a value, because the window re-derives orientation from inside the
        // platform's own resize handling, long after this. See
        // GameWindow::SetLogicalSizeProviderEXT.
        {
            auto* const game = game_;
            game_->getWindowProperty().SetLogicalSizeProviderEXT(
                [game](int& width, int& height)
                {
                    return game->getGraphicsDeviceProperty().GetLogicalSizeEXT(width, height);
                });
        }

        registerServices();
        game_->getWindowProperty().keyboardOrientationChanged_ = [this]
        { onKeyboardOrientationChanged(); };

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
        // CLR finalization leaves managed objects alone. C++ destruction must
        // additionally detach callbacks whose target is this raw object.
        if (disposed_)
        {
            // Dispose retains the XNA manager registration. Its raw C++ pointer must not
            // survive destruction (XNA Dispose(bool) IL_0011-0043 removes only the service).
            if (game_ != nullptr &&
                game_->getServicesProperty().GetService<IGraphicsDeviceManager>() == this)
                game_->getServicesProperty().RemoveService<IGraphicsDeviceManager>();
        }
        else
        {
            if (game_ != nullptr) game_->getWindowProperty().keyboardOrientationChanged_ = {};
            unregisterServices();
            frameContextLease_.reset();
            drawBegun_ = false;
            unsubscribeDeviceEvents();
        }
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
        if (value <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "value", "BackBufferWidth and BackBufferHeight must be greater than zero.");
        }
        preferredBackBufferHeight_ = value;
        markPreferencesChanged();
    }

    SharpRuntime::intcs GraphicsDeviceManager::getPreferredBackBufferWidthProperty() const
    {
        return preferredBackBufferWidth_;
    }

    void GraphicsDeviceManager::setPreferredBackBufferWidthProperty(SharpRuntime::intcs value)
    {
        if (value <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "value", "BackBufferWidth and BackBufferHeight must be greater than zero.");
        }
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
        if (graphicsDevice_ == nullptr || !hasCreatedDevice_)
        {
            CreateDevice();
            return;
        }

        if (!prefsChanged_ && !useResizedBackBuffer_)
        {
            return;
        }

        // Refresh ApplyChanges IL_0011-0013 -> ChangeDevice(false), IL_0081-0083:
        // virtual selection precedes preparation and the reset/create decision.
        GraphicsDeviceInformation gdi = FindBestDevice(false);
        {
            // XNA ChangeDevice IL_00b6-00bf raises this for the device change itself.
            PreparingDeviceSettingsEventArgs args(gdi);
            OnPreparingDeviceSettings(this, args);
        }

        // XNA ChangeDevice IL_00c4-0120: a profile mismatch (or virtual refusal)
        // takes creation, including its second preparation of this same candidate.
        if (!CanResetDevice(gdi))
        {
            createDevice(gdi);
            return;
        }

        if (game_ != nullptr)
        {
            if (supportsOrientations())
            {
                game_->getWindowProperty().SetSupportedOrientations(
                    GameWindow::resolveSupportedOrientations(supportedOrientations_,
                        preferredBackBufferWidth_, preferredBackBufferHeight_));
            }

            auto& parameters = gdi.getPresentationParametersProperty();
            game_->getWindowProperty().BeginScreenDeviceChange(parameters.getIsFullScreenProperty());
            game_->getWindowProperty().EndScreenDeviceChange(
                gdi.getAdapterProperty() != nullptr ? gdi.getAdapterProperty()->getDeviceNameProperty() : std::string(),
                parameters.getBackBufferWidthProperty(),
                parameters.getBackBufferHeightProperty()
            );
        }

        // ApplyToExistingRenderer() below calls GraphicsDevice::Reset(), which
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
        // XNA IGraphicsDeviceManager.CreateDevice -> ChangeDevice(true), IL_00ab-00ac.
        createDevice(FindBestDevice(true));
    }

    void GraphicsDeviceManager::createDevice(GraphicsDeviceInformation gdi)
    {
        if (game_ == nullptr)
            throw System::InvalidOperationException(
                "GraphicsDeviceManager cannot create a GraphicsDevice without a Game-owned device in this CNA renderer.");

        const bool replacing = hasCreatedDevice_;
        if (replacing && graphicsDevice_ != nullptr)
        {
            game_->PreserveDeviceWindowForReplacement();
            // XNA CreateDevice IL_0000-0015: notify/dispose before preparation, then clear.
            graphicsDevice_->Dispose();
            unsubscribeDeviceEvents();
            graphicsDevice_ = nullptr;
        }
        {
            // XNA CreateDevice(GraphicsDeviceInformation) IL_001a-0022.
            PreparingDeviceSettingsEventArgs args(gdi);
            OnPreparingDeviceSettings(this, args);
        }

        if (game_ != nullptr)
        {
            auto& pp = gdi.getPresentationParametersProperty();

            if (supportsOrientations())
            {
                game_->getWindowProperty().SetSupportedOrientations(
                    GameWindow::resolveSupportedOrientations(supportedOrientations_,
                        preferredBackBufferWidth_, preferredBackBufferHeight_));
            }

            game_->getWindowProperty().BeginScreenDeviceChange(pp.getIsFullScreenProperty());
            game_->getWindowProperty().EndScreenDeviceChange(
                gdi.getAdapterProperty() != nullptr ? gdi.getAdapterProperty()->getDeviceNameProperty() : std::string(),
                pp.getBackBufferWidthProperty(),
                pp.getBackBufferHeightProperty()
            );
        }

        if (replacing)
        {
            game_->ReplaceGraphicsDevice(*gdi.getAdapterProperty(), gdi.getGraphicsProfileProperty(),
                                         gdi.getPresentationParametersProperty());
            graphicsDevice_ = &game_->getGraphicsDeviceProperty();
            graphicsDevice_->SetPresentationMode(static_cast<int>(preferredPresentationMode_));
        }
        else
        {
            graphicsDevice_ = &game_->getGraphicsDeviceProperty();
            applyToExistingRenderer(gdi);
        }
        hasCreatedDevice_ = true;

        // XNA CreateDevice IL_0059-00b0 subscribes the new device before DeviceCreated.
        // The first eager CNA device is configured before these forwarding hooks are installed.
        if (!deviceEventsSubscribed_)
        {
            deviceDisposingToken_ = graphicsDevice_->Disposing.Add(
                [this](System::Object* sender, const System::EventArgs& args)
                {
                    // A game may explicitly dispose its device from Draw(). Release the frame's
                    // renderer-context lease while the renderer and its native context still
                    // exist; EndDraw must then neither present nor release a dangling lease.
                    frameContextLease_.reset();
                    drawBegun_ = false;
                    OnDeviceDisposing(sender, args);
                });
            deviceResettingToken_ = graphicsDevice_->DeviceResetting.Add(
                [this](System::Object* sender, const System::EventArgs& args) { OnDeviceResetting(sender, args); });
            deviceResetToken_ = graphicsDevice_->DeviceReset.Add(
                [this](System::Object* sender, const System::EventArgs& args) { OnDeviceReset(sender, args); });
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

        // XNA EnsureDevicePlatform IL_0000-0016 reads IsActiveIgnoringGuide. The
        // applied presentation mode (ChangeDevice IL_0143-014b), not the preference, gates draw.
        if (game_ != nullptr && !game_->IsActive_ &&
            graphicsDevice_->getPresentationParametersProperty().getIsFullScreenProperty())
            return false;

        // Native recovery may need another frame before drawing is safe. Poll through the
        // renderer's gate before entering Draw, while allowing ordinary Update ticks to continue.
        if (!graphicsDevice_->GetRenderer().TryBeginDrawEXT())
            return false;

        frameContextLease_ = graphicsDevice_->AcquireRendererThreadContextLeaseForFrame();
        drawBegun_ = true;
        return true;
    }

    void GraphicsDeviceManager::EndDraw()
    {
        if (graphicsDevice_ != nullptr && drawBegun_)
        {
            drawBegun_ = false;
            try
            {
                graphicsDevice_->Present();
            }
            catch (...)
            {
                frameContextLease_.reset();
                throw;
            }
            frameContextLease_.reset();
        }
    }

    const std::string& GraphicsDeviceManager::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GraphicsDeviceManager";
        return typeName;
    }

    void GraphicsDeviceManager::Dispose(bool disposing)
    {
        // XNA GraphicsDeviceManager.Dispose(bool), IL_0000..IL_0006.
        if (!disposing) return;
        if (disposed_)
        {
            // XNA GraphicsDeviceManager.Dispose(bool), IL_00b6..IL_00cf, notifies on
            // every explicit call even after the device pointer has been cleared.
            if (disposing) Disposed.Raise(this, System::EventArgs::Empty);
            return;
        }

        if (game_ != nullptr) game_->getWindowProperty().keyboardOrientationChanged_ = {};
        // XNA Dispose(bool) IL_0011-0043: retain IGraphicsDeviceManager until destruction.
        unregisterServices(false);

        frameContextLease_.reset();
        drawBegun_ = false;
        if (disposing && graphicsDevice_ != nullptr)
        {
            // XNA GraphicsDeviceManager.Dispose(bool), IL_009c..IL_00b5, disposes
            // the device before clearing it. C++ ownership controls deletion only.
            graphicsDevice_->Dispose();
        }
        unsubscribeDeviceEvents();

        if (disposing && graphicsDevice_ != nullptr)
        {
            if (ownsGraphicsDevice_)
            {
                delete graphicsDevice_;
                ownsGraphicsDevice_ = false;
            }
            graphicsDevice_ = nullptr;
        }

        disposed_ = true;
        if (disposing) Disposed.Raise(this, System::EventArgs::Empty);
    }

    void GraphicsDeviceManager::unsubscribeDeviceEvents()
    {
        if (!deviceEventsSubscribed_ || graphicsDevice_ == nullptr)
        {
            return;
        }

        graphicsDevice_->Disposing.Remove(deviceDisposingToken_);
        graphicsDevice_->DeviceResetting.Remove(deviceResettingToken_);
        graphicsDevice_->DeviceReset.Remove(deviceResetToken_);
        deviceEventsSubscribed_ = false;
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
        // observable once the device-event forwarding calls these with the managed
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
        // XNA CanResetDevice IL_0000-0016: a reset keeps the device, so it must already have the
        // requested profile (FNA throws NotImplementedException). XNA reads the device
        // unconditionally; CNA answers false without one.
        return graphicsDevice_ != nullptr &&
               graphicsDevice_->getGraphicsProfileProperty() == newDeviceInfo.getGraphicsProfileProperty();
    }

    GraphicsDeviceInformation GraphicsDeviceManager::FindBestDevice(bool anySuitableDevice)
    {
        // CNA enumerates one default-adapter candidate; FNA throws NotImplementedException.
        // XNA FindBestPlatformDevice IL_0061-0083 invokes the virtual ranking hook,
        // refuses an empty result (including subclass filtering), then selects index zero.
        (void)anySuitableDevice;

        GraphicsDeviceInformation gdi;
        gdi.setAdapterProperty(&Graphics::GraphicsAdapter::getDefaultAdapterProperty());
        Graphics::PresentationParameters parameters;
        // XNA AddDevices(bool, List) IL_0066-006d: the candidate presents into the game's window.
        if (game_ != nullptr)
        {
            parameters.setDeviceWindowHandleProperty(game_->getWindowProperty().getHandleProperty());
        }
        gdi.setPresentationParametersProperty(parameters);
        INTERNAL_CreateGraphicsDeviceInformation(gdi);
        std::vector<GraphicsDeviceInformation> candidates{gdi};
        RankDevices(candidates);
        if (candidates.empty())
            throw Graphics::NoSuitableGraphicsDeviceException(
                "The process of ranking devices removed all compatible devices.");
        return candidates.front();
    }

    void GraphicsDeviceManager::RankDevices(std::vector<GraphicsDeviceInformation>& foundDevices)
    {
        // XNA RankDevices IL_0000-0007 delegates to the platform sort without filtering.
        // FNA throws NotImplementedException here.
        if (foundDevices.size() > 1)
        {
            try
            {
                sortDevices(*this, foundDevices, 0, static_cast<std::ptrdiff_t>(foundDevices.size()) - 1);
            }
            catch (const System::IndexOutOfRangeException&)
            {
                // CLR ArraySortHelper<T>.Sort IL_001b-004a reports an inconsistent comparer.
                throw System::ArgumentException("Unable to sort because the comparer is inconsistent.");
            }
            catch (const System::Exception&)
            {
                // CLR ArraySortHelper<T>.Sort IL_004b-005c retains the comparison failure.
                throw System::InvalidOperationException(
                    "Failed to compare two elements in the array.", std::current_exception());
            }
        }
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
        if (game_ != nullptr)
        {
            auto& window = game_->getWindowProperty();
            if (window.keyboardOrientationEnabled_)
                pp.setDisplayOrientationProperty(window.selectKeyboardOrientation(
                    supportedOrientations_, preferredBackBufferWidth_, preferredBackBufferHeight_));
            // A device that rotates is created in an orientation the game supports, so the swap
            // below gives a portrait game its portrait back buffer; left Default, every game got
            // a landscape one.
            else if (supportsOrientations_)
                pp.setDisplayOrientationProperty(window.selectDeviceOrientation(
                    supportedOrientations_, preferredBackBufferWidth_, preferredBackBufferHeight_));
            else if (window.keyboardOrientationResetPending_)
                pp.setDisplayOrientationProperty(DisplayOrientation::Default);
        }

        if (useResizedBackBuffer_)
        {
            pp.setBackBufferWidthProperty(resizedBackBufferWidth_);
            pp.setBackBufferHeightProperty(resizedBackBufferHeight_);
            useResizedBackBuffer_ = false;
        }
        else
        {
            if (!supportsOrientations())
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
    }

    bool GraphicsDeviceManager::supportsOrientations() const
    {
        return supportsOrientations_ || (game_ != nullptr &&
            game_->getWindowProperty().keyboardOrientationEnabled_);
    }

    void GraphicsDeviceManager::onKeyboardOrientationChanged()
    {
        if (disposed_) return;
        auto& window = game_->getWindowProperty();
        if (window.keyboardOrientationEnabled_ && deviceEventsSubscribed_ &&
            window.selectKeyboardOrientation(supportedOrientations_, preferredBackBufferWidth_,
                preferredBackBufferHeight_) == window.getCurrentOrientationProperty() &&
            graphicsDevice_->getPresentationParametersProperty().getDisplayOrientationProperty() ==
                window.getCurrentOrientationProperty()) return;
        markPreferencesChanged();
        // Constructor opt-ins must not create/reset the device before Game initializes it.
        if (deviceEventsSubscribed_) ApplyChanges();
    }

    void GraphicsDeviceManager::markPreferencesChanged()
    {
        prefsChanged_ = true;
    }

    void GraphicsDeviceManager::registerServices()
    {
        // Wire window resize → viewport update. Subscribed before the registrations (XNA hooks it
        // after them, unobservably) so that a refused AddService leaves the destructor this
        // manager's own token to remove, not the default 0 another subscriber may hold.
        clientSizeChangedToken_ = game_->getWindowProperty().ClientSizeChanged.Add(
            [this](System::Object* sender, const System::EventArgs& args)
            {
                INTERNAL_OnClientSizeChanged(sender, args);
            });

        game_->getServicesProperty().AddService<IGraphicsDeviceManager>(this);
        game_->getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(this);
    }

    void GraphicsDeviceManager::unregisterServices(bool removeManager)
    {
        if (game_ == nullptr)
        {
            return;
        }

        game_->getWindowProperty().ClientSizeChanged.Remove(clientSizeChangedToken_);
        clientSizeChangedToken_ = 0;
        if (removeManager && game_->getServicesProperty().GetService<IGraphicsDeviceManager>() == this)
            game_->getServicesProperty().RemoveService<IGraphicsDeviceManager>();
        if (game_->getServicesProperty().GetService<Graphics::IGraphicsDeviceService>() == this)
            game_->getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
    }

    void GraphicsDeviceManager::applyToExistingRenderer(GraphicsDeviceInformation& gdi)
    {
        if (graphicsDevice_ == nullptr)
        {
            return;
        }

        auto& pp = gdi.getPresentationParametersProperty();

        // DX9 D9-103 finding: Game's own GraphicsDevice_ member is eagerly
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
        // DeviceResetting/DeviceReset events. After CreateDevice() installs the subscriptions
        // above, this manager forwards those device events as its own; the initial reset occurs
        // before subscription and is followed only by DeviceCreated, matching FNA creation.
        graphicsDevice_->Reset(pp, *gdi.getAdapterProperty());

        graphicsDevice_->UpdateViewportFromWindow();
        if (game_ != nullptr)
        {
            auto& window = game_->getWindowProperty();
            if (window.keyboardOrientationEnabled_)
                window.setCurrentOrientationProperty(pp.getDisplayOrientationProperty());
            else if (window.keyboardOrientationResetPending_)
            {
                window.keyboardOrientationResetPending_ = false;
                window.updateFromPlatform();
            }
        }
    }
}
