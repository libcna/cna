// SPDX-License-Identifier: MS-PL

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Platform/PlatformCapabilities.hpp"
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/IDrawable.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "Microsoft/Xna/Framework/IGraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
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

namespace CNA::Internal
{
    class GameTestPeer;
}

// Forward-declared rather than included: Game.hpp is included by every game, and the platform
// contract is an implementation detail of how the loop is driven, not part of the XNA surface a
// game writes against. The unique_ptr member below is why Game's destructor is defined out of
// line.
namespace CNA::Platform
{
    class IPlatform;
}

namespace Microsoft::Xna::Framework
{
    /** @brief Base class that provides the XNA-style game loop, services, window, content and components. */
    class Game : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Raised when the game becomes active. */
        System::EventHandler<System::EventArgs> Activated;

        /** @brief Raised when the game becomes inactive. */
        System::EventHandler<System::EventArgs> Deactivated;

        /** @brief Raised when the game has been disposed. */
        System::EventHandler<System::EventArgs> Disposed;

        /** @brief Raised when the game is exiting. */
        System::EventHandler<System::EventArgs> Exiting;

        /** @brief Creates a new Game instance on the build-time platform and initialises it. */
        Game();

        /** @brief Destructor; calls Dispose(false) to release unmanaged resources. */
        ~Game() override;

        /**
         * @brief Gets the game component collection.
         * @return A reference to the mutable game component collection.
         */
        [[nodiscard]] GameComponentCollection& getComponentsProperty();

        /**
         * @brief Gets the game component collection.
         * @return A const reference to the game component collection.
         */
        [[nodiscard]] const GameComponentCollection& getComponentsProperty() const;

        /**
         * @brief Gets the content manager.
         * @return A reference to the mutable content manager.
         */
        [[nodiscard]] Content::ContentManager& getContentProperty();

        /**
         * @brief Gets the content manager.
         * @return A const reference to the content manager.
         */
        [[nodiscard]] const Content::ContentManager& getContentProperty() const;

        /**
         * @brief Sets the content manager.
         * @param value The new content manager to assign.
         */
        void setContentProperty(const Content::ContentManager& value);

        /**
         * @brief Gets the graphics device from the graphics device service when available.
         * @return A reference to the graphics device.
         */
        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();

        /**
         * @brief Gets the time spent sleeping when the game is inactive.
         * @return A const reference to the inactive sleep time.
         */
        [[nodiscard]] const System::TimeSpan& getInactiveSleepTimeProperty() const;

        /**
         * @brief Sets the time spent sleeping when the game is inactive.
         * @param value The new inactive sleep time; must be non-negative.
         */
        void setInactiveSleepTimeProperty(const System::TimeSpan& value);

        /**
         * @brief Gets whether the game window is active.
         * @return true if the game window currently has focus.
         */
        [[nodiscard]] bool getIsActiveProperty() const;

        /**
         * @brief Gets whether the game uses a fixed timestep.
         * @return true if the game loop uses a fixed timestep.
         */
        [[nodiscard]] bool getIsFixedTimeStepProperty() const;

        /**
         * @brief Sets whether the game uses a fixed timestep.
         * @param value true to enable fixed timestep; false for variable timestep.
         */
        void setIsFixedTimeStepProperty(bool value);

        /**
         * @brief Gets whether the mouse cursor is visible.
         * @return true if the mouse cursor is visible.
         */
        [[nodiscard]] bool getIsMouseVisibleProperty() const;

        /**
         * @brief Sets whether the mouse cursor is visible.
         * @param value true to show the mouse cursor; false to hide it.
         */
        void setIsMouseVisibleProperty(bool value);

        /**
         * @brief Gets command-line launch parameters.
         * @return A reference to the mutable launch parameters map.
         */
        [[nodiscard]] LaunchParameters& getLaunchParametersProperty();

        /**
         * @brief Gets command-line launch parameters.
         * @return A const reference to the launch parameters map.
         */
        [[nodiscard]] const LaunchParameters& getLaunchParametersProperty() const;

        /**
         * @brief Gets the target elapsed time between updates.
         * @return A const reference to the target elapsed time.
         */
        [[nodiscard]] const System::TimeSpan& getTargetElapsedTimeProperty() const;

        /**
         * @brief Sets the target elapsed time between updates.
         * @param value The desired time between updates; must be positive and non-zero.
         */
        void setTargetElapsedTimeProperty(const System::TimeSpan& value);

        /**
         * @brief Gets the service container.
         * @return A reference to the mutable service container.
         */
        [[nodiscard]] GameServiceContainer& getServicesProperty();

        /**
         * @brief Gets the service container.
         * @return A const reference to the service container.
         */
        [[nodiscard]] const GameServiceContainer& getServicesProperty() const;

        /**
         * @brief Gets the game window.
         * @return A reference to the mutable game window.
         */
        [[nodiscard]] GameWindow& getWindowProperty();

        /**
         * @brief Gets the game window.
         * @return A const reference to the game window.
         */
        [[nodiscard]] const GameWindow& getWindowProperty() const;

        /** @brief Signals the game loop to exit. */
        void Exit();

        /** @brief Resets elapsed timing for the next variable-timestep update. */
        void ResetElapsedTime();

        /** @brief Skips drawing for the current frame. */
        void SuppressDraw();

        /** @brief Runs exactly one frame. */
        void RunOneFrame();

        /**
         * @brief Runs the game loop until Exit is called.
         */
        void Run();

        /** @brief Advances one game tick. */
        void Tick();

        /** @brief Disposes the game. */
        void Dispose() override;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Compatibility helper used by existing CNA examples.
         * @return The current target frames per second.
         */
        [[nodiscard]] CNAEXT double getTargetFPSProperty() const;

        /**
         * @brief Compatibility helper used by existing CNA examples.
         * @return The current target milliseconds per frame.
         */
        [[nodiscard]] CNAEXT double getTargetMsFrameTimeProperty() const;

        /**
         * @brief Converts frames-per-second to milliseconds per frame.
         * @param framesPerSecond The frame rate to convert.
         * @return The equivalent milliseconds per frame.
         */
        [[nodiscard]] CNAEXT static double fpsToMillisecondsPerFrame(SharpRuntime::intcs framesPerSecond);

        /**
         * @brief Gets the platform this game runs on.
         *
         * The game owns exactly one platform instance, created when the game is constructed and
         * destroyed with it. Subsystems that a `Game` owns reach the platform through here rather
         * than through `CNA::Platform::GetCurrentPlatform()`: the ambient accessor exists for the
         * static parts of the XNA API (`Keyboard::GetState`, `StorageDevice`) that genuinely
         * cannot be handed a context, and using it where a context *is* available would make the
         * dependency invisible.
         *
         * @return The platform, which lives as long as this game does.
         */
        CNAEXT [[nodiscard]] CNA::Platform::IPlatform& GetPlatformEXT() const;

        /**
         * @brief Gets the capabilities captured from this game's platform during construction.
         *
         * Games use this to select optional UI/input paths once at startup.  It deliberately
         * returns a cached value: platform capability discovery may probe a host terminal, so it
         * must never become a per-frame virtual query.
         */
        CNAEXT [[nodiscard]] const CNA::Platform::PlatformCapabilities&
        GetPlatformCapabilitiesEXT() const;

        /** @brief Internal loop flag matching the FNA/XNA Game implementation shape. */
        CNAEXT bool RunApplication;

    protected:
        /**
         * @brief Creates a game that owns an explicitly supplied platform.
         *
         * This CNAEXT seam is primarily for embedding hosts and cross-implementation tests. The
         * platform is installed before any other Game member is constructed, exactly like the
         * build-time default used by Game(), and remains owned by the Game until its teardown.
         *
         * @param platform Platform instance to own; must not be null.
         * @throws std::invalid_argument If @p platform is null.
         */
        CNAEXT explicit Game(std::unique_ptr<CNA::Platform::IPlatform> platform);

        /** @brief Called before the game loop starts. */
        virtual void BeginRun();

        /** @brief Called after the game loop ends. */
        virtual void EndRun();

        /**
         * @brief Called before drawing. Returning false skips Draw/EndDraw.
         * @return true if drawing should proceed; false to skip the current frame.
         */
        virtual bool BeginDraw();

        /** @brief Called after drawing. */
        virtual void EndDraw();

        /** @brief Loads graphics and content resources. */
        virtual void LoadContent();

        /** @brief Unloads graphics and content resources. */
        virtual void UnloadContent();

        /** @brief Initializes the game and current components. */
        virtual void Initialize();

        /**
         * @brief Draws the game and drawable components.
         * @param gameTime Snapshot of the game timing state.
         */
        virtual void Draw(const GameTime& gameTime);

        /**
         * @brief Updates the game and updateable components.
         * @param gameTime Snapshot of the game timing state.
         */
        virtual void Update(GameTime& gameTime);

        /**
         * @brief Raises the Exiting event.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnExiting(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Raises the Activated event.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnActivated(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Raises the Deactivated event.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnDeactivated(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Displays a missing-requirement error when the platform can do so.
         * @param exception The exception that describes the missing requirement.
         * @return true if the error was shown; false otherwise.
         */
        virtual bool ShowMissingRequirementMessage(const std::exception& exception);

        /**
         * @brief Performs disposal.
         * @param disposing true when called from Dispose(); false when called from the destructor.
         */
        virtual void Dispose(bool disposing);

    private:
        friend class CNA::Internal::GameTestPeer;

        struct PlatformEventBatch;
        struct PlatformInstallation;

        // Declared before every other member, and therefore constructed first and destroyed last.
        // The graphics device, the window and the content manager may all reach the platform
        // during their own construction or teardown, so the platform's lifetime has to strictly
        // contain theirs. Member order is the only thing that guarantees that.
        std::unique_ptr<CNA::Platform::IPlatform> platform_;

        // PLAT-46 follow-up: undoes the ambient installation if construction does not complete.
        // Installing from platform_'s own initialiser is what lets later members find the game's
        // platform instead of lazily creating a second one -- but it also means a throw from any
        // of those members (a renderer refusing the platform it was handed, say) unwinds without
        // ever running ~Game, leaving the process-wide accessor aimed at a platform that is
        // destroyed moments later during that same unwind. This member is constructed immediately
        // after the installation and disarmed once the constructor completes, so exactly the
        // failed-construction path is covered and the normal path still uninstalls from ~Game.
        std::unique_ptr<PlatformInstallation> platformInstallation_;

        CNA::Platform::PlatformCapabilities platformCapabilities_;
        std::unique_ptr<PlatformEventBatch> eventBatch_;

        GameComponentCollection Components_;
        Graphics::GraphicsDevice GraphicsDevice_;
        Content::ContentManager Content_;
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

        Graphics::IGraphicsDeviceService* graphicsDeviceService_;
        IGraphicsDeviceManager* graphicsDeviceManager_;
        Graphics::GraphicsAdapter* currentAdapter_;

        bool hasInitialized_;
        bool suppressDraw_;
        bool isDisposed_;
        bool forceElapsedTimeToZero_;

        // Mobile lifecycle (plans/plan_apple.md APPLE-7): true between the platform's
        // WillEnterBackground and DidEnterForeground notifications. Only ever set on Android and
        // iOS -- no other platform raises those transitions.
        bool isSuspended_;

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

        std::unordered_map<IUpdateable*, System::EventHandler<System::EventArgs>::Token> updateOrderChangedTokens_;
        std::unordered_map<IDrawable*, System::EventHandler<System::EventArgs>::Token> drawOrderChangedTokens_;

        // FNA: internal set — only Game itself may change the active state.
        void setIsActiveProperty(bool value);

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
        void WaitWhileSuspended();

        void OnComponentAdded(System::Object* sender, const GameComponentCollectionEventArgs& args);
        void OnComponentRemoved(System::Object* sender, const GameComponentCollectionEventArgs& args);
        void OnUpdateOrderChanged(System::Object* sender, const System::EventArgs& args);
        void OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args);

#if defined(__EMSCRIPTEN__)
    private:
        // Emscripten loop state. RunLoop yields through Asyncify between frames until the frame body
        // clears its game pointer, retaining the public Run() call's blocking lifetime semantics.
        struct EmscriptenLoopState;
        static EmscriptenLoopState s_emLoopState;
        static void EmscriptenMainLoopCallback();
#endif
    };
}
