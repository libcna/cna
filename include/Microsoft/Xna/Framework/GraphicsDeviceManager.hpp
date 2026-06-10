// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "Microsoft/Xna/Framework/IGraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/PreparingDeviceSettingsEventArgs.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

struct SDL_Window;

namespace Microsoft::Xna::Framework
{
    class Game;

    /// CNA-specific presentation/scaling policy for the rendering backend.
    NOXNA enum class PresentationMode
    {
        /// Scale with black bars to preserve aspect ratio.
        Letterbox = 0,
        /// Scale with cropping to fill the screen.
        Overscan = 1,
        /// Stretch to fill the screen ignoring aspect ratio.
        Stretch = 2,
        /// Render at the native back-buffer size without scaling.
        NativeBackBuffer = 3,
        /// Fix the back-buffer height and scale the width to the display aspect ratio.
        FixedHeightDynamicWidth = 4
    };

    /// Manages the graphics device and applies high-level presentation settings.
    class GraphicsDeviceManager : public System::Object,
                                  public Graphics::IGraphicsDeviceService,
                                  public System::IDisposable,
                                  public IGraphicsDeviceManager
    {
    public:
        /// Default back buffer width used when no explicit width is set.
        static constexpr SharpRuntime::intcs DefaultBackBufferWidth = 800;
        /// Default back buffer height used when no explicit height is set.
        static constexpr SharpRuntime::intcs DefaultBackBufferHeight = 480;

        /// Raised when this manager is disposed.
        System::EventHandler<System::EventArgs> Disposed;

        /// Raised after the graphics device has been created.
        System::EventHandler<System::EventArgs> DeviceCreated;

        /// Raised before the graphics device is disposed.
        System::EventHandler<System::EventArgs> DeviceDisposing;

        /// Raised after the graphics device has reset.
        System::EventHandler<System::EventArgs> DeviceReset;

        /// Raised before the graphics device resets.
        System::EventHandler<System::EventArgs> DeviceResetting;

        /// Raised before device settings are applied, allowing callers to adjust them.
        System::EventHandler<PreparingDeviceSettingsEventArgs> PreparingDeviceSettings;

        /// Creates an empty graphics device manager with no associated Game.
        NOXNA GraphicsDeviceManager();

        /// Creates a graphics device manager for the specified game.
        explicit GraphicsDeviceManager(Game* game);

        /// Destructor; calls Dispose(false).
        ~GraphicsDeviceManager() override;

        /// Gets the requested graphics feature profile.
        [[nodiscard]] Graphics::GraphicsProfile getGraphicsProfileProperty() const;

        /// Sets the requested graphics feature profile.
        void setGraphicsProfileProperty(Graphics::GraphicsProfile value);

        /// Gets the managed graphics device.
        [[nodiscard]] Graphics::GraphicsDevice* getGraphicsDeviceProperty() const override;

        /// Returns the DeviceCreated event.
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() override;

        /// Returns the DeviceDisposing event.
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() override;

        /// Returns the DeviceReset event.
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResetEvent() override;

        /// Returns the DeviceResetting event.
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResettingEvent() override;

        /// Gets whether the graphics device should be fullscreen.
        [[nodiscard]] bool getIsFullScreenProperty() const;

        /// Sets whether the graphics device should be fullscreen.
        void setIsFullScreenProperty(bool value);

        /// Gets whether multisampling is preferred.
        [[nodiscard]] bool getPreferMultiSamplingProperty() const;

        /// Sets whether multisampling is preferred.
        void setPreferMultiSamplingProperty(bool value);

        /// Gets the preferred back buffer format.
        [[nodiscard]] Graphics::SurfaceFormat getPreferredBackBufferFormatProperty() const;

        /// Sets the preferred back buffer format.
        void setPreferredBackBufferFormatProperty(Graphics::SurfaceFormat value);

        /// Gets the preferred back buffer height.
        [[nodiscard]] SharpRuntime::intcs getPreferredBackBufferHeightProperty() const;

        /// Sets the preferred back buffer height.
        void setPreferredBackBufferHeightProperty(SharpRuntime::intcs value);

        /// Gets the preferred back buffer width.
        [[nodiscard]] SharpRuntime::intcs getPreferredBackBufferWidthProperty() const;

        /// Sets the preferred back buffer width.
        void setPreferredBackBufferWidthProperty(SharpRuntime::intcs value);

        /// Gets the preferred depth/stencil format.
        [[nodiscard]] Graphics::DepthFormat getPreferredDepthStencilFormatProperty() const;

        /// Sets the preferred depth/stencil format.
        void setPreferredDepthStencilFormatProperty(Graphics::DepthFormat value);

        /// Gets whether presenting should synchronize with vertical retrace.
        [[nodiscard]] bool getSynchronizeWithVerticalRetraceProperty() const;

        /// Sets whether presenting should synchronize with vertical retrace.
        void setSynchronizeWithVerticalRetraceProperty(bool value);

        /// Gets supported display orientations.
        [[nodiscard]] DisplayOrientation getSupportedOrientationsProperty() const;

        /// Sets supported display orientations.
        void setSupportedOrientationsProperty(DisplayOrientation value);

        /// Gets the CNA presentation/scaling policy.
        NOXNA [[nodiscard]] PresentationMode getPreferredPresentationModeProperty() const;

        /// Sets the CNA presentation/scaling policy.
        NOXNA void setPreferredPresentationModeProperty(PresentationMode value);

        /// Applies pending graphics setting changes.
        void ApplyChanges();

        /// Toggles fullscreen state and applies the change.
        void ToggleFullScreen();

        /// Releases this manager and the graphics device it owns, if any.
        void Dispose() override;

        /// Creates or recreates the graphics device.
        void CreateDevice() override;

        /// Begins drawing for the current frame.
        [[nodiscard]] bool BeginDraw() override;

        /// Ends drawing and presents the frame.
        void EndDraw() override;

        /// Returns the fully-qualified .NET type name of this class.
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /// Performs disposal. When disposing is true, managed-style resources are released.
        virtual void Dispose(bool disposing);

        /// Raises DeviceCreated.
        virtual void OnDeviceCreated(System::Object* sender, const System::EventArgs& args);

        /// Raises DeviceDisposing.
        virtual void OnDeviceDisposing(System::Object* sender, const System::EventArgs& args);

        /// Raises DeviceReset.
        virtual void OnDeviceReset(System::Object* sender, const System::EventArgs& args);

        /// Raises DeviceResetting.
        virtual void OnDeviceResetting(System::Object* sender, const System::EventArgs& args);

        /// Raises PreparingDeviceSettings.
        virtual void OnPreparingDeviceSettings(System::Object* sender, PreparingDeviceSettingsEventArgs& args);

        /// Returns whether the current graphics device can reset to the supplied settings.
        virtual bool CanResetDevice(const GraphicsDeviceInformation& newDeviceInfo);

        /// Finds the best graphics device settings for this game.
        virtual GraphicsDeviceInformation FindBestDevice(bool anySuitableDevice);

        /// Ranks candidate graphics device settings.
        virtual void RankDevices(std::vector<GraphicsDeviceInformation>& foundDevices);

    private:
        Game* game_;
        Graphics::GraphicsDevice* graphicsDevice_;
        bool ownsGraphicsDevice_;
        bool drawBegun_;
        bool disposed_;
        bool prefsChanged_;
        bool supportsOrientations_;
        bool useResizedBackBuffer_;
        SharpRuntime::intcs resizedBackBufferWidth_;
        SharpRuntime::intcs resizedBackBufferHeight_;

        Graphics::GraphicsProfile graphicsProfile_;
        bool isFullScreen_;
        bool preferMultiSampling_;
        Graphics::SurfaceFormat preferredBackBufferFormat_;
        SharpRuntime::intcs preferredBackBufferHeight_;
        SharpRuntime::intcs preferredBackBufferWidth_;
        Graphics::DepthFormat preferredDepthStencilFormat_;
        bool synchronizeWithVerticalRetrace_;
        DisplayOrientation supportedOrientations_;
        PresentationMode preferredPresentationMode_;

        void INTERNAL_OnClientSizeChanged(System::Object* sender, const System::EventArgs& args);
        void INTERNAL_CreateGraphicsDeviceInformation(GraphicsDeviceInformation& gdi);
        void markPreferencesChanged();
        void registerServices();
        void unregisterServices();
        [[nodiscard]] SDL_Window* tryGetSDLWindow() const;
        void applyToExistingBackend(GraphicsDeviceInformation& gdi);
    };
}
