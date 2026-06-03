#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/ExitingEventArgs.hpp"
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/IDrawable.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "Microsoft/Xna/Framework/IGraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/IUpdateable.hpp"
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework
{
    /// Base class that provides the XNA-style game loop, services, window, content and components.
    class Game : public System::Object, public System::IDisposable
    {
    public:
        /// Raised when the game becomes active.
        System::EventHandler<System::EventArgs> Activated;

        /// Raised when the game becomes inactive.
        System::EventHandler<System::EventArgs> Deactivated;

        /// Raised when the game has been disposed.
        System::EventHandler<System::EventArgs> Disposed;

        /// Raised when the game is exiting.
        System::EventHandler<ExitingEventArgs> Exiting;

        Game();
        ~Game() override;

        /// Gets the game component collection.
        [[nodiscard]] GameComponentCollection& getComponentsProperty();

        /// Gets the game component collection.
        [[nodiscard]] const GameComponentCollection& getComponentsProperty() const;

        /// Gets the content manager.
        [[nodiscard]] Content::ContentManager& getContentProperty();

        /// Gets the content manager.
        [[nodiscard]] const Content::ContentManager& getContentProperty() const;

        /// Sets the content manager.
        void setContentProperty(const Content::ContentManager& value);

        /// Gets the graphics device from the graphics device service when available.
        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();

        /// Gets the time spent sleeping when the game is inactive.
        [[nodiscard]] const System::TimeSpan& getInactiveSleepTimeProperty() const;

        /// Sets the time spent sleeping when the game is inactive.
        void setInactiveSleepTimeProperty(const System::TimeSpan& value);

        /// Gets whether the game window is active.
        [[nodiscard]] bool getIsActiveProperty() const;

        /// Sets whether the game window is active and raises activation events.
        void setIsActiveProperty(bool value);

        /// Gets whether the game uses a fixed timestep.
        [[nodiscard]] bool getIsFixedTimeStepProperty() const;

        /// Sets whether the game uses a fixed timestep.
        void setIsFixedTimeStepProperty(bool value);

        /// Gets whether the mouse cursor is visible.
        [[nodiscard]] bool getIsMouseVisibleProperty() const;

        /// Sets whether the mouse cursor is visible.
        void setIsMouseVisibleProperty(bool value);

        /// Gets command-line launch parameters.
        [[nodiscard]] LaunchParameters& getLaunchParametersProperty();

        /// Gets command-line launch parameters.
        [[nodiscard]] const LaunchParameters& getLaunchParametersProperty() const;

        /// Gets the target elapsed time between updates.
        [[nodiscard]] const System::TimeSpan& getTargetElapsedTimeProperty() const;

        /// Sets the target elapsed time between updates.
        void setTargetElapsedTimeProperty(const System::TimeSpan& value);

        /// Gets the service container.
        [[nodiscard]] GameServiceContainer& getServicesProperty();

        /// Gets the service container.
        [[nodiscard]] const GameServiceContainer& getServicesProperty() const;

        /// Gets the game window.
        [[nodiscard]] GameWindow& getWindowProperty();

        /// Gets the game window.
        [[nodiscard]] const GameWindow& getWindowProperty() const;

        /// Signals the game loop to exit.
        void Exit();

        /// Resets elapsed timing for the next variable-timestep update.
        void ResetElapsedTime();

        /// Skips drawing for the current frame.
        void SuppressDraw();

        /// Runs exactly one frame.
        void RunOneFrame();

        /// Runs the game loop until Exit is called.
        void Run();

        /// Advances one game tick.
        void Tick();

        /// Disposes the game.
        void Dispose() override;

        /// Compatibility helper used by existing CNA examples.
        [[nodiscard]] NOXNA double getTargetFPSProperty() const;

        /// Compatibility helper used by existing CNA examples.
        [[nodiscard]] NOXNA double getTargetMsFrameTimeProperty() const;

        /// Converts frames-per-second to milliseconds per frame.
        [[nodiscard]] NOXNA static double fpsToMillisecondsPerFrame(SharpRuntime::intcs framesPerSecond);

        /// Internal loop flag matching the FNA/XNA Game implementation shape.
        bool RunApplication;

    protected:
        /// Called before the game loop starts.
        virtual void BeginRun();

        /// Called after the game loop ends.
        virtual void EndRun();

        /// Called before drawing. Returning false skips Draw/EndDraw.
        virtual bool BeginDraw();

        /// Called after drawing.
        virtual void EndDraw();

        /// Loads graphics and content resources.
        virtual void LoadContent();

        /// Unloads graphics and content resources.
        virtual void UnloadContent();

        /// Initializes the game and current components.
        virtual void Initialize();

        /// Draws the game and drawable components.
        virtual void Draw(const GameTime& gameTime);

        /// Updates the game and updateable components.
        virtual void Update(GameTime& gameTime);

        /// Raises the Exiting event.
        virtual void OnExiting(System::Object* sender, const ExitingEventArgs& args);

        /// Raises the Activated event.
        virtual void OnActivated(System::Object* sender, const System::EventArgs& args);

        /// Raises the Deactivated event.
        virtual void OnDeactivated(System::Object* sender, const System::EventArgs& args);

        /// Displays a missing-requirement error when the platform can do so.
        virtual bool ShowMissingRequirementMessage(const std::exception& exception);

        /// Performs disposal.
        virtual void Dispose(bool disposing);

    private:
        GameComponentCollection Components_;
        Content::ContentManager Content_;
        Graphics::GraphicsDevice GraphicsDevice_;
        GameWindow Window_;
        LaunchParameters LaunchParameters_;
        GameServiceContainer Services_;

        System::TimeSpan InactiveSleepTime_;
        bool IsActive_;
        bool IsFixedTimeStep_;
        bool IsMouseVisible_;
        System::TimeSpan TargetElapsedTime_;

        std::vector<IUpdateable*> updateableComponents_;
        std::vector<IUpdateable*> currentlyUpdatingComponents_;
        std::vector<IDrawable*> drawableComponents_;
        std::vector<IDrawable*> currentlyDrawingComponents_;

        IGraphicsDeviceService* graphicsDeviceService_;
        IGraphicsDeviceManager* graphicsDeviceManager_;
        Graphics::GraphicsAdapter* currentAdapter_;

        bool hasInitialized_;
        bool suppressDraw_;
        bool isDisposed_;
        bool forceElapsedTimeToZero_;

        GameTime gameTime_;
        std::uint64_t previousPerformanceCounter_;
        System::TimeSpan accumulatedElapsedTime_;
        int updateFrameLag_;

        static constexpr int PREVIOUS_SLEEP_TIME_COUNT = 128;
        static constexpr int SLEEP_TIME_MASK = PREVIOUS_SLEEP_TIME_COUNT - 1;
        std::array<System::TimeSpan, PREVIOUS_SLEEP_TIME_COUNT> previousSleepTimes_;
        int sleepTimeIndex_;
        System::TimeSpan worstCaseSleepPrecision_;

        static const System::TimeSpan MaxElapsedTime;

        void AssertNotDisposed() const;
        void DoInitialize();
        void CategorizeComponent(IGameComponent* component);
        void SortUpdateable(IUpdateable* updateable);
        void SortDrawable(IDrawable* drawable);
        void BeforeLoop();
        void AfterLoop();
        void RunLoop();
        System::TimeSpan AdvanceElapsedTime();
        void UpdateEstimatedSleepPrecision(const System::TimeSpan& timeSpentSleeping);
        void PollEvents();

        void OnComponentAdded(System::Object* sender, const GameComponentCollectionEventArgs& args);
        void OnComponentRemoved(System::Object* sender, const GameComponentCollectionEventArgs& args);
        void OnUpdateOrderChanged(System::Object* sender, const System::EventArgs& args);
        void OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args);

#if defined(__EMSCRIPTEN__)
    private:
        // Emscripten non-blocking main loop support.
        // The callback is a static C-compatible function passed to
        // emscripten_set_main_loop(); it reads/writes s_emLoopState.
        struct EmscriptenLoopState;
        static EmscriptenLoopState s_emLoopState;
        static void EmscriptenMainLoopCallback();
#endif
    };
}
