// SPDX-License-Identifier: MS-PL

#pragma once

#include <memory>
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

namespace CNA::Internal::Renderers
{
    class IRendererThreadContextLease;
}

namespace Microsoft::Xna::Framework
{
    class Game;

    /** @brief CNA-specific presentation/scaling policy for the rendering renderer. */
    CNAEXT enum class PresentationMode
    {
        /** @brief Scale with black bars to preserve aspect ratio. */
        Letterbox = 0,
        /** @brief Scale with cropping to fill the screen. */
        Overscan = 1,
        /** @brief Stretch to fill the screen ignoring aspect ratio. */
        Stretch = 2,
        /** @brief Render at the native back-buffer size without scaling. */
        NativeBackBuffer = 3,
        /** @brief Fix the back-buffer height and scale the width to the display aspect ratio. */
        FixedHeightDynamicWidth = 4
    };

    /** @brief Manages the graphics device and applies high-level presentation settings. */
    class GraphicsDeviceManager : public System::Object,
                                  public Graphics::IGraphicsDeviceService,
                                  public System::IDisposable,
                                  public IGraphicsDeviceManager
    {
    public:
        /** @brief Default back buffer width used when no explicit width is set. */
        static constexpr SharpRuntime::intcs DefaultBackBufferWidth = 800;
        /** @brief Default back buffer height used when no explicit height is set. */
        static constexpr SharpRuntime::intcs DefaultBackBufferHeight = 480;

        /** @brief Raised when this manager is disposed. */
        System::EventHandler<System::EventArgs> Disposed;

        /** @brief Raised after the graphics device has been created. */
        System::EventHandler<System::EventArgs> DeviceCreated;

        /** @brief Raised before the graphics device is disposed. */
        System::EventHandler<System::EventArgs> DeviceDisposing;

        /** @brief Raised after the graphics device has reset. */
        System::EventHandler<System::EventArgs> DeviceReset;

        /** @brief Raised before the graphics device resets. */
        System::EventHandler<System::EventArgs> DeviceResetting;

        /** @brief Raised before device settings are applied, allowing callers to adjust them. */
        System::EventHandler<PreparingDeviceSettingsEventArgs> PreparingDeviceSettings;

        /** @brief Creates an empty graphics device manager with no associated Game. */
        CNAEXT GraphicsDeviceManager();

        /**
         * @brief Creates a graphics device manager for the specified game.
         * @param game Pointer to the game that owns this manager; must not be null.
         */
        explicit GraphicsDeviceManager(Game* game);

        /** @brief Destructor; calls Dispose(false). */
        ~GraphicsDeviceManager() override;

        /**
         * @brief Gets the requested graphics feature profile.
         * @return The current graphics profile.
         */
        [[nodiscard]] Graphics::GraphicsProfile getGraphicsProfileProperty() const;

        /**
         * @brief Sets the requested graphics feature profile.
         * @param value The desired graphics profile.
         */
        void setGraphicsProfileProperty(Graphics::GraphicsProfile value);

        /**
         * @brief Gets the managed graphics device.
         * @return Pointer to the graphics device, or nullptr if not yet created.
         */
        [[nodiscard]] Graphics::GraphicsDevice* getGraphicsDeviceProperty() const override;

        /**
         * @brief Returns the DeviceCreated event.
         * @return A reference to the DeviceCreated event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() override;

        /**
         * @brief Returns the DeviceDisposing event.
         * @return A reference to the DeviceDisposing event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() override;

        /**
         * @brief Returns the DeviceReset event.
         * @return A reference to the DeviceReset event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResetEvent() override;

        /**
         * @brief Returns the DeviceResetting event.
         * @return A reference to the DeviceResetting event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResettingEvent() override;

        /**
         * @brief Gets whether the graphics device should be fullscreen.
         * @return true if fullscreen mode is requested.
         */
        [[nodiscard]] bool getIsFullScreenProperty() const;

        /**
         * @brief Sets whether the graphics device should be fullscreen.
         * @param value true to request fullscreen; false for windowed mode.
         */
        void setIsFullScreenProperty(bool value);

        /**
         * @brief Gets whether multisampling is preferred.
         * @return true if multisampling is preferred.
         */
        [[nodiscard]] bool getPreferMultiSamplingProperty() const;

        /**
         * @brief Sets whether multisampling is preferred.
         * @param value true to request multisampling; false to disable it.
         */
        void setPreferMultiSamplingProperty(bool value);

        /**
         * @brief Gets the preferred back buffer format.
         * @return The preferred surface format for the back buffer.
         */
        [[nodiscard]] Graphics::SurfaceFormat getPreferredBackBufferFormatProperty() const;

        /**
         * @brief Sets the preferred back buffer format.
         * @param value The desired surface format for the back buffer.
         */
        void setPreferredBackBufferFormatProperty(Graphics::SurfaceFormat value);

        /**
         * @brief Gets the preferred back buffer height.
         * @return The preferred height in pixels.
         */
        [[nodiscard]] SharpRuntime::intcs getPreferredBackBufferHeightProperty() const;

        /**
         * @brief Sets the preferred back buffer height.
         * @param value The desired height in pixels.
         */
        void setPreferredBackBufferHeightProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the preferred back buffer width.
         * @return The preferred width in pixels.
         */
        [[nodiscard]] SharpRuntime::intcs getPreferredBackBufferWidthProperty() const;

        /**
         * @brief Sets the preferred back buffer width.
         * @param value The desired width in pixels.
         */
        void setPreferredBackBufferWidthProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the preferred depth/stencil format.
         * @return The preferred depth/stencil format.
         */
        [[nodiscard]] Graphics::DepthFormat getPreferredDepthStencilFormatProperty() const;

        /**
         * @brief Sets the preferred depth/stencil format.
         * @param value The desired depth/stencil format.
         */
        void setPreferredDepthStencilFormatProperty(Graphics::DepthFormat value);

        /**
         * @brief Gets whether presenting should synchronize with vertical retrace.
         * @return true if vertical sync is enabled.
         */
        [[nodiscard]] bool getSynchronizeWithVerticalRetraceProperty() const;

        /**
         * @brief Sets whether presenting should synchronize with vertical retrace.
         * @param value true to enable vertical sync; false to disable it.
         */
        void setSynchronizeWithVerticalRetraceProperty(bool value);

        /**
         * @brief Gets supported display orientations.
         * @return The bitmask of supported display orientations.
         */
        [[nodiscard]] DisplayOrientation getSupportedOrientationsProperty() const;

        /**
         * @brief Sets supported display orientations.
         * @param value Bitmask of the orientations the game supports.
         */
        void setSupportedOrientationsProperty(DisplayOrientation value);

        /**
         * @brief Gets the CNA presentation/scaling policy.
         * @return The current presentation mode.
         */
        CNAEXT [[nodiscard]] PresentationMode getPreferredPresentationModeProperty() const;

        /**
         * @brief Sets the CNA presentation/scaling policy.
         * @param value The desired presentation mode.
         */
        CNAEXT void setPreferredPresentationModeProperty(PresentationMode value);

        /** @brief Applies pending graphics setting changes. */
        void ApplyChanges();

        /** @brief Toggles fullscreen state and applies the change. */
        void ToggleFullScreen();

        /** @brief Releases this manager and the graphics device it owns, if any. */
        void Dispose() override;

        /** @brief Creates or recreates the graphics device. */
        void CreateDevice() override;

        /**
         * @brief Begins drawing for the current frame.
         * @return true if drawing should proceed; false if no device is available.
         */
        [[nodiscard]] bool BeginDraw() override;

        /** @brief Ends drawing and presents the frame. */
        void EndDraw() override;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Performs disposal. When disposing is true, managed-style resources are released.
         * @param disposing true when called from Dispose(); false when called from the destructor.
         */
        virtual void Dispose(bool disposing);

        /**
         * @brief Raises DeviceCreated.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnDeviceCreated(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Raises DeviceDisposing.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnDeviceDisposing(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Raises DeviceReset.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnDeviceReset(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Raises DeviceResetting.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnDeviceResetting(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Raises PreparingDeviceSettings.
         * @param sender The object that originated the event.
         * @param args Mutable event data containing the device settings to adjust.
         */
        virtual void OnPreparingDeviceSettings(System::Object* sender, PreparingDeviceSettingsEventArgs& args);

        /**
         * @brief Returns whether the current graphics device can reset to the supplied settings.
         * @param newDeviceInfo The candidate device settings.
         * @return true if a reset is possible; false otherwise.
         */
        virtual bool CanResetDevice(const GraphicsDeviceInformation& newDeviceInfo);

        /**
         * @brief Finds the best graphics device settings for this game.
         * @param anySuitableDevice true to accept any suitable device; false for strict matching.
         * @return The best matching GraphicsDeviceInformation.
         */
        virtual GraphicsDeviceInformation FindBestDevice(bool anySuitableDevice);

        /**
         * @brief Ranks candidate graphics device settings.
         * @param foundDevices The list of candidate device settings to rank in place.
         */
        virtual void RankDevices(std::vector<GraphicsDeviceInformation>& foundDevices);

    private:
        Game* game_;
        Graphics::GraphicsDevice* graphicsDevice_;
        bool ownsGraphicsDevice_;
        bool deviceEventsSubscribed_;
        bool drawBegun_;
        std::unique_ptr<CNA::Internal::Renderers::IRendererThreadContextLease> frameContextLease_;
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
        void applyToExistingRenderer(GraphicsDeviceInformation& gdi);
    };
}
