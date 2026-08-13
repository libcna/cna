// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Game.hpp"

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Logger.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace Microsoft::Xna::Framework
{
    namespace
    {
        void InitAudio()
        {
#ifdef SOUND_ENABLED
            if (!MIX_Init())
            {
                throw std::runtime_error(std::string("MIX_Init failed: ") + SDL_GetError());
            }
#endif
        }

        void ShutdownAudio()
        {
#ifdef SOUND_ENABLED
            MIX_Quit();
#endif
        }

        [[nodiscard]] double TotalMilliseconds(const System::TimeSpan& value)
        {
            return value.getTotalMillisecondsProperty();
        }

        [[nodiscard]] bool TimeSpanGreater(const System::TimeSpan& left, const System::TimeSpan& right)
        {
            return TotalMilliseconds(left) > TotalMilliseconds(right);
        }

        [[nodiscard]] bool TimeSpanGreaterOrEqual(const System::TimeSpan& left, const System::TimeSpan& right)
        {
            return TotalMilliseconds(left) >= TotalMilliseconds(right);
        }

        [[nodiscard]] bool TimeSpanLess(const System::TimeSpan& left, const System::TimeSpan& right)
        {
            return TotalMilliseconds(left) < TotalMilliseconds(right);
        }

        [[nodiscard]] bool TimeSpanLessOrEqual(const System::TimeSpan& left, const System::TimeSpan& right)
        {
            return TotalMilliseconds(left) <= TotalMilliseconds(right);
        }

        [[nodiscard]] System::TimeSpan TimeSpanMin(const System::TimeSpan& left, const System::TimeSpan& right)
        {
            return TimeSpanLess(left, right) ? left : right;
        }

        [[nodiscard]] System::TimeSpan TimeSpanMax(const System::TimeSpan& left, const System::TimeSpan& right)
        {
            return TimeSpanGreater(left, right) ? left : right;
        }

        void RemoveUpdateable(std::vector<IUpdateable*>& values, IUpdateable* value)
        {
            values.erase(std::remove(values.begin(), values.end(), value), values.end());
        }

        void RemoveDrawable(std::vector<IDrawable*>& values, IDrawable* value)
        {
            values.erase(std::remove(values.begin(), values.end(), value), values.end());
        }
    }

    const System::TimeSpan Game::MaxElapsedTime = System::TimeSpan::FromMilliseconds(500.0);

    const std::string& Game::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Game";
        return typeName;
    }

    Game::Game()
        : Components_(),
          GraphicsDevice_(),
          Content_(),
          Window_(),
          LaunchParameters_(),
          Services_(),
          InactiveSleepTime_(System::TimeSpan::FromSeconds(0.02)),
          IsActive_(false),
          IsFixedTimeStep_(true),
          IsMouseVisible_(false),
          TargetElapsedTime_(System::TimeSpan(166667L)),
          updateableComponents_(),
          currentlyUpdatingComponents_(),
          drawableComponents_(),
          currentlyDrawingComponents_(),
          graphicsDeviceService_(nullptr),
          graphicsDeviceManager_(nullptr),
          currentAdapter_(nullptr),
          hasInitialized_(false),
          suppressDraw_(false),
          isDisposed_(false),
          forceElapsedTimeToZero_(false),
          gameTime_(),
          previousPerformanceCounter_(0),
          accumulatedElapsedTime_(System::TimeSpan::Zero),
          updateFrameLag_(0),
          previousSleepTimes_(),
          sleepTimeIndex_(0),
          worstCaseSleepPrecision_(System::TimeSpan::FromMilliseconds(1.0)),
          RunApplication(true)
    {
        for (auto& previousSleepTime : previousSleepTimes_)
        {
            previousSleepTime = System::TimeSpan::FromMilliseconds(1.0);
        }

        Window_.setWindowInternal(GraphicsDevice_.GetRenderer().GetWindowInternal());
        Content_.setGraphicsDevice(GraphicsDevice_);

        FrameworkDispatcher::Update();
        InitAudio();
    }

    Game::~Game()
    {
        Dispose(false);
        ShutdownAudio();
    }

    GameComponentCollection& Game::getComponentsProperty()
    {
        return Components_;
    }

    const GameComponentCollection& Game::getComponentsProperty() const
    {
        return Components_;
    }

    Content::ContentManager& Game::getContentProperty()
    {
        return Content_;
    }

    const Content::ContentManager& Game::getContentProperty() const
    {
        return Content_;
    }

    void Game::setContentProperty(const Content::ContentManager& value)
    {
        Content_ = value;
    }

    Graphics::GraphicsDevice& Game::getGraphicsDeviceProperty()
    {
        if (graphicsDeviceService_ == nullptr)
        {
            graphicsDeviceService_ = Services_.GetService<Graphics::IGraphicsDeviceService>();
        }

        if (graphicsDeviceService_ != nullptr && graphicsDeviceService_->getGraphicsDeviceProperty() != nullptr)
        {
            return *graphicsDeviceService_->getGraphicsDeviceProperty();
        }

        return GraphicsDevice_;
    }

    const System::TimeSpan& Game::getInactiveSleepTimeProperty() const
    {
        return InactiveSleepTime_;
    }

    void Game::setInactiveSleepTimeProperty(const System::TimeSpan& value)
    {
        if (TimeSpanLess(value, System::TimeSpan::Zero))
        {
            throw std::out_of_range("InactiveSleepTime must be positive.");
        }

        InactiveSleepTime_ = value;
    }

    bool Game::getIsActiveProperty() const
    {
        return IsActive_;
    }

    void Game::setIsActiveProperty(bool value)
    {
        if (IsActive_ == value)
        {
            return;
        }

        IsActive_ = value;

        if (IsActive_)
        {
            OnActivated(this, System::EventArgs::Empty);
        }
        else
        {
            OnDeactivated(this, System::EventArgs::Empty);
        }
    }

    bool Game::getIsFixedTimeStepProperty() const
    {
        return IsFixedTimeStep_;
    }

    void Game::setIsFixedTimeStepProperty(bool value)
    {
        IsFixedTimeStep_ = value;
    }

    bool Game::getIsMouseVisibleProperty() const
    {
        return IsMouseVisible_;
    }

    void Game::setIsMouseVisibleProperty(bool value)
    {
        if (IsMouseVisible_ == value)
        {
            return;
        }

        IsMouseVisible_ = value;

        if (GraphicsDevice_.GetWindowInternal() != nullptr)
        {
            if (value) {
                SDL_ShowCursor();
            } else
            {
                SDL_HideCursor();
            }
        }
    }

    LaunchParameters& Game::getLaunchParametersProperty()
    {
        return LaunchParameters_;
    }

    const LaunchParameters& Game::getLaunchParametersProperty() const
    {
        return LaunchParameters_;
    }

    const System::TimeSpan& Game::getTargetElapsedTimeProperty() const
    {
        return TargetElapsedTime_;
    }

    void Game::setTargetElapsedTimeProperty(const System::TimeSpan& value)
    {
        if (TimeSpanLessOrEqual(value, System::TimeSpan::Zero))
        {
            throw std::out_of_range("TargetElapsedTime must be positive and non-zero.");
        }

        TargetElapsedTime_ = value;
    }

    GameServiceContainer& Game::getServicesProperty()
    {
        return Services_;
    }

    const GameServiceContainer& Game::getServicesProperty() const
    {
        return Services_;
    }

    GameWindow& Game::getWindowProperty()
    {
        return Window_;
    }

    const GameWindow& Game::getWindowProperty() const
    {
        return Window_;
    }

    void Game::Exit()
    {
        RunApplication = false;
        suppressDraw_ = true;
    }

    void Game::ResetElapsedTime()
    {
        if (!IsFixedTimeStep_)
        {
            forceElapsedTimeToZero_ = true;
        }
    }

    void Game::SuppressDraw()
    {
        suppressDraw_ = true;
    }

    void Game::RunOneFrame()
    {
        if (!hasInitialized_)
        {
            DoInitialize();
            previousPerformanceCounter_ = SDL_GetPerformanceCounter();
            hasInitialized_ = true;
        }

        Tick();
    }

    void Game::Run()
    {
        AssertNotDisposed();

        if (!hasInitialized_)
        {
            DoInitialize();
            hasInitialized_ = true;
        }

        BeginRun();
        BeforeLoop();

        previousPerformanceCounter_ = SDL_GetPerformanceCounter();
        RunLoop();

        EndRun();
        AfterLoop();
    }

    void Game::Tick()
    {
        AdvanceElapsedTime();

        if (IsFixedTimeStep_)
        {
            while (TimeSpanLess(accumulatedElapsedTime_ + worstCaseSleepPrecision_, TargetElapsedTime_))
            {
                SDL_Delay(1);
                const System::TimeSpan timeAdvancedSinceSleeping = AdvanceElapsedTime();
                UpdateEstimatedSleepPrecision(timeAdvancedSinceSleeping);
            }

            // FNA uses Thread.SpinWait(1); yield() is the nearest C++ equivalent.
            while (TimeSpanLess(accumulatedElapsedTime_, TargetElapsedTime_))
            {
                std::this_thread::yield();
                AdvanceElapsedTime();
            }
        }

        PollEvents();

        if (TimeSpanGreater(accumulatedElapsedTime_, MaxElapsedTime))
        {
            accumulatedElapsedTime_ = MaxElapsedTime;
        }

        if (IsFixedTimeStep_)
        {
            gameTime_.setElapsedGameTimeProperty(TargetElapsedTime_);
            int stepCount = 0;

            while (TimeSpanGreaterOrEqual(accumulatedElapsedTime_, TargetElapsedTime_))
            {
                gameTime_.setTotalGameTimeProperty(gameTime_.getTotalGameTimeProperty() + TargetElapsedTime_);
                accumulatedElapsedTime_ = accumulatedElapsedTime_ - TargetElapsedTime_;
                ++stepCount;

                AssertNotDisposed();
                Update(gameTime_);
            }

            updateFrameLag_ += std::max(0, stepCount - 1);

            if (gameTime_.getIsRunningSlowlyProperty())
            {
                if (updateFrameLag_ == 0)
                {
                    gameTime_.setIsRunningSlowlyProperty(false);
                }
            }
            else if (updateFrameLag_ >= 5)
            {
                gameTime_.setIsRunningSlowlyProperty(true);
            }

            if (stepCount == 1 && updateFrameLag_ > 0)
            {
                --updateFrameLag_;
            }

            // FNA: TimeSpan.FromTicks(TargetElapsedTime.Ticks * stepCount) — integer ticks, no float error.
            gameTime_.setElapsedGameTimeProperty(
                System::TimeSpan::FromTicks(TargetElapsedTime_.getTicksProperty() * stepCount)
            );
        }
        else
        {
            if (forceElapsedTimeToZero_)
            {
                gameTime_.setElapsedGameTimeProperty(System::TimeSpan::Zero);
                forceElapsedTimeToZero_ = false;
            }
            else
            {
                gameTime_.setElapsedGameTimeProperty(accumulatedElapsedTime_);
                gameTime_.setTotalGameTimeProperty(gameTime_.getTotalGameTimeProperty() + gameTime_.getElapsedGameTimeProperty());
            }

            accumulatedElapsedTime_ = System::TimeSpan::Zero;
            AssertNotDisposed();
            Update(gameTime_);
        }

        if (suppressDraw_)
        {
            suppressDraw_ = false;
        }
        else if (BeginDraw())
        {
            Draw(gameTime_);
            EndDraw();
        }
    }

    void Game::Dispose()
    {
        Dispose(true);
        Disposed.Raise(this, System::EventArgs::Empty);
    }

    double Game::getTargetFPSProperty() const
    {
        const double msPerFrame = getTargetMsFrameTimeProperty();
        return msPerFrame <= 0.0 ? 0.0 : 1000.0 / msPerFrame;
    }

    double Game::getTargetMsFrameTimeProperty() const
    {
        return TargetElapsedTime_.getTotalMillisecondsProperty();
    }

    double Game::fpsToMillisecondsPerFrame(SharpRuntime::intcs framesPerSecond)
    {
        return framesPerSecond <= 0 ? 0.0 : 1000.0 / static_cast<double>(framesPerSecond);
    }

    void Game::BeginRun()
    {
    }

    void Game::EndRun()
    {
    }

    bool Game::BeginDraw()
    {
        if (graphicsDeviceManager_ != nullptr)
        {
            return graphicsDeviceManager_->BeginDraw();
        }

        return true;
    }

    void Game::EndDraw()
    {
        if (graphicsDeviceManager_ != nullptr)
        {
            graphicsDeviceManager_->EndDraw();
        }
        else
        {
            getGraphicsDeviceProperty().Present();
        }
    }

    void Game::LoadContent()
    {
    }

    void Game::UnloadContent()
    {
    }

    void Game::Initialize()
    {
        for (SharpRuntime::intcs i = 0; i < Components_.getCountProperty(); ++i)
        {
            IGameComponent* component = Components_[i];
            if (component != nullptr)
            {
                component->Initialize();
            }
        }

        graphicsDeviceService_ = Services_.GetService<Graphics::IGraphicsDeviceService>();

        if (graphicsDeviceService_ != nullptr)
        {
            // REMED-CORE-006: FNA's Initialize() (Game.cs:649-662) subscribes
            // graphicsDeviceService.DeviceDisposing += (o,e) => UnloadContent(); -- CNA never
            // performed this subscription, so the documented UnloadContent() lifecycle hook was
            // dead code under every circumstance.
            graphicsDeviceService_->getDeviceDisposingEvent() +=
                [this](System::Object*, const System::EventArgs&) { UnloadContent(); };
        }

        if (graphicsDeviceService_ == nullptr || graphicsDeviceService_->getGraphicsDeviceProperty() != nullptr)
        {
            LoadContent();
        }
        else
        {
            // FNA fidelity: a registered IGraphicsDeviceService whose device is not yet available
            // defers LoadContent() until DeviceCreated fires, rather than skipping it.
            graphicsDeviceService_->getDeviceCreatedEvent() +=
                [this](System::Object*, const System::EventArgs&) { LoadContent(); };
        }
    }

    void Game::Draw(const GameTime& gameTime)
    {
        currentlyDrawingComponents_.clear();
        currentlyDrawingComponents_.insert(
            currentlyDrawingComponents_.end(),
            drawableComponents_.begin(),
            drawableComponents_.end()
        );

        for (IDrawable* drawable : currentlyDrawingComponents_)
        {
            if (drawable != nullptr && drawable->getVisibleProperty())
            {
                drawable->Draw(gameTime);
            }
        }

        currentlyDrawingComponents_.clear();
    }

    void Game::Update(GameTime& gameTime)
    {
        currentlyUpdatingComponents_.clear();
        currentlyUpdatingComponents_.insert(
            currentlyUpdatingComponents_.end(),
            updateableComponents_.begin(),
            updateableComponents_.end()
        );

        for (IUpdateable* updateable : currentlyUpdatingComponents_)
        {
            if (updateable != nullptr && updateable->getEnabledProperty())
            {
                updateable->Update(gameTime);
            }
        }

        currentlyUpdatingComponents_.clear();

        FrameworkDispatcher::Update();
    }

    void Game::OnExiting(System::Object* sender, const System::EventArgs& args)
    {
        (void) sender;
        Exiting.Raise(this, args);
    }

    void Game::OnActivated(System::Object* sender, const System::EventArgs& args)
    {
        (void) sender;
        AssertNotDisposed();
        Activated.Raise(this, args);
    }

    void Game::OnDeactivated(System::Object* sender, const System::EventArgs& args)
    {
        (void) sender;
        AssertNotDisposed();
        Deactivated.Raise(this, args);
    }

    bool Game::ShowMissingRequirementMessage(const std::exception& exception)
    {
        (void) exception;
        return false;
    }

    void Game::Dispose(bool disposing)
    {
        if (isDisposed_)
        {
            return;
        }

        if (disposing)
        {
            for (SharpRuntime::intcs i = 0; i < Components_.getCountProperty(); ++i)
            {
                if (auto* disposable = dynamic_cast<System::IDisposable*>(Components_[i]))
                {
                    disposable->Dispose();
                }
            }

            Content_.Dispose();

            if (graphicsDeviceService_ != nullptr)
            {
                if (auto* disposable = dynamic_cast<System::IDisposable*>(graphicsDeviceService_))
                {
                    disposable->Dispose();
                }
            }

            // P8-002: mirrors FNA's ProgramExit, which quits SDL_INIT_VIDEO | SDL_INIT_GAMEPAD
            // together. GraphicsDevice::Dispose (above) already quit SDL_INIT_VIDEO; this is the
            // SDL_INIT_GAMEPAD counterpart, since that subsystem is Input's own responsibility
            // (DoInitialize() below is the matching startup call).
            CNA::Internal::Input::SdlInputBridge::ShutdownGamepadSubsystem();
        }

        isDisposed_ = true;
    }

    void Game::AssertNotDisposed() const
    {
        if (isDisposed_)
        {
            throw std::runtime_error("The Game object was used after being disposed.");
        }
    }

    void Game::DoInitialize()
    {
        AssertNotDisposed();

        graphicsDeviceManager_ = Services_.GetService<IGraphicsDeviceManager>();
        if (graphicsDeviceManager_ != nullptr)
        {
            graphicsDeviceManager_->CreateDevice();
        }

        // Startup invariant: initialize the SDL gamepad subsystem here — DoInitialize() runs once,
        // before the first event pump and before the first Update() (via both Run() and
        // RunOneFrame()). This makes gamepads that were already connected before the first frame get
        // enumerated by SDL (which queues SDL_EVENT_GAMEPAD_ADDED for each), so they are visible to
        // GamePad::GetState from frame one. SdlInputBridge::ProcessEvent still calls this lazily as a
        // defensive fallback for any host that pumps events without going through Game's loop.
        CNA::Internal::Input::SdlInputBridge::EnsureGamepadSubsystemInitialized();

        Initialize();

        updateableComponents_.clear();
        drawableComponents_.clear();

        for (SharpRuntime::intcs i = 0; i < Components_.getCountProperty(); ++i)
        {
            CategorizeComponent(Components_[i]);
        }

        Components_.ComponentAdded += [this](System::Object* s, const GameComponentCollectionEventArgs& a)
        {
            OnComponentAdded(s, a);
        };
        Components_.ComponentRemoved += [this](System::Object* s, const GameComponentCollectionEventArgs& a)
        {
            OnComponentRemoved(s, a);
        };
    }

    void Game::CategorizeComponent(IGameComponent* component)
    {
        if (component == nullptr)
        {
            return;
        }

        if (auto* updateable = dynamic_cast<IUpdateable*>(component))
        {
            SortUpdateable(updateable);
            updateOrderChangedTokens_[updateable] = updateable->getUpdateOrderChangedEvent().Add(
                [this](System::Object* s, const System::EventArgs& a) { OnUpdateOrderChanged(s, a); }
            );
        }

        if (auto* drawable = dynamic_cast<IDrawable*>(component))
        {
            SortDrawable(drawable);
            drawOrderChangedTokens_[drawable] = drawable->getDrawOrderChangedEvent().Add(
                [this](System::Object* s, const System::EventArgs& a) { OnDrawOrderChanged(s, a); }
            );
        }
    }

    void Game::SortUpdateable(IUpdateable* updateable)
    {
        if (updateable == nullptr)
        {
            return;
        }

        RemoveUpdateable(updateableComponents_, updateable);

        const auto it = std::find_if(
            updateableComponents_.begin(),
            updateableComponents_.end(),
            [updateable](IUpdateable* existing)
            {
                return existing != nullptr &&
                       updateable->getUpdateOrderProperty() < existing->getUpdateOrderProperty();
            }
        );

        updateableComponents_.insert(it, updateable);
    }

    void Game::SortDrawable(IDrawable* drawable)
    {
        if (drawable == nullptr)
        {
            return;
        }

        RemoveDrawable(drawableComponents_, drawable);

        const auto it = std::find_if(
            drawableComponents_.begin(),
            drawableComponents_.end(),
            [drawable](IDrawable* existing)
            {
                return existing != nullptr &&
                       drawable->getDrawOrderProperty() < existing->getDrawOrderProperty();
            }
        );

        drawableComponents_.insert(it, drawable);
    }

#if defined(__EMSCRIPTEN__)
    struct Game::EmscriptenLoopState
    {
        Game* game = nullptr;
        GameTime gameTime;
        std::uint64_t lastTickMs = 0;
        double accumulatorMs = 0.0;
    };

    Game::EmscriptenLoopState Game::s_emLoopState;

    void Game::EmscriptenMainLoopCallback()
    {
        try
        {
            EmscriptenLoopState& state = s_emLoopState;
            if (state.game == nullptr)
            {
                return;
            }

            state.game->PollEvents();

            const std::uint64_t nowMs = SDL_GetTicks();
            if (state.lastTickMs == 0)
            {
                state.lastTickMs = nowMs;
            }

            double deltaMs = static_cast<double>(nowMs - state.lastTickMs);
            state.lastTickMs = nowMs;

            if (deltaMs > 250.0)
            {
                deltaMs = 250.0;
            }

            state.accumulatorMs += deltaMs;
            const double targetMs = state.game->getTargetMsFrameTimeProperty();
            const auto stepSpan = System::TimeSpan::FromMilliseconds(targetMs);

            bool updated = false;
            while (state.accumulatorMs >= targetMs)
            {
                state.accumulatorMs -= targetMs;

                state.gameTime.setElapsedGameTimeProperty(stepSpan);
                state.gameTime.setTotalGameTimeProperty(state.gameTime.getTotalGameTimeProperty() + stepSpan);
                state.gameTime.setIsRunningSlowlyProperty(false);

                state.game->Update(state.gameTime);
                updated = true;
            }

            if (updated && state.game->BeginDraw())
            {
                state.game->Draw(state.gameTime);
                state.game->EndDraw();
            }

            if (!state.game->RunApplication)
            {
                emscripten_cancel_main_loop();
                state.game->OnExiting(state.game, System::EventArgs::Empty);
                state.game = nullptr;
            }
        }
        catch (const std::exception& exception)
        {
            emscripten_cancel_main_loop();
            s_emLoopState.game = nullptr;
            CNA::Logger::Error(
                std::string("CNA: fatal exception in Emscripten main loop: ") + exception.what());
        }
        catch (...)
        {
            emscripten_cancel_main_loop();
            s_emLoopState.game = nullptr;
            CNA::Logger::Error("CNA: unknown fatal exception in Emscripten main loop");
        }
    }
#endif

    void Game::BeforeLoop()
    {
        setIsActiveProperty(true);
    }

    void Game::AfterLoop()
    {
    }

    void Game::RunLoop()
    {
#if defined(__EMSCRIPTEN__)
        s_emLoopState.game = this;
        s_emLoopState.gameTime = gameTime_;
        emscripten_set_main_loop(EmscriptenMainLoopCallback, 0, 1);
#else
        while (RunApplication)
        {
            Tick();
        }

        OnExiting(this, System::EventArgs::Empty);
#endif
    }

    System::TimeSpan Game::AdvanceElapsedTime()
    {
        // FNA uses System.Diagnostics.Stopwatch; CNA uses SDL_GetPerformanceCounter for equivalent high-resolution timing.
        const std::uint64_t currentCounter = SDL_GetPerformanceCounter();

        if (previousPerformanceCounter_ == 0)
        {
            previousPerformanceCounter_ = currentCounter;
            return System::TimeSpan::Zero;
        }

        const std::uint64_t frequency = SDL_GetPerformanceFrequency();
        const double elapsedMilliseconds =
            (static_cast<double>(currentCounter - previousPerformanceCounter_) * 1000.0) /
            static_cast<double>(frequency);

        previousPerformanceCounter_ = currentCounter;

        const System::TimeSpan timeAdvanced = System::TimeSpan::FromMilliseconds(elapsedMilliseconds);
        accumulatedElapsedTime_ = accumulatedElapsedTime_ + timeAdvanced;
        return timeAdvanced;
    }

    void Game::UpdateEstimatedSleepPrecision(const System::TimeSpan& timeSpentSleeping)
    {
        const System::TimeSpan upperTimeBound = System::TimeSpan::FromMilliseconds(4.0);
        const System::TimeSpan cappedSleepTime = TimeSpanMin(timeSpentSleeping, upperTimeBound);

        if (TimeSpanGreaterOrEqual(cappedSleepTime, worstCaseSleepPrecision_))
        {
            worstCaseSleepPrecision_ = cappedSleepTime;
        }
        else if (TotalMilliseconds(previousSleepTimes_[sleepTimeIndex_]) == TotalMilliseconds(worstCaseSleepPrecision_))
        {
            System::TimeSpan maxSleepTime = System::TimeSpan::Zero;
            for (const auto& previousSleepTime : previousSleepTimes_)
            {
                maxSleepTime = TimeSpanMax(maxSleepTime, previousSleepTime);
            }
            worstCaseSleepPrecision_ = maxSleepTime;
        }

        previousSleepTimes_[sleepTimeIndex_] = cappedSleepTime;
        sleepTimeIndex_ = (sleepTimeIndex_ + 1) & SLEEP_TIME_MASK;
    }

    void Game::PollEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            CNA::Internal::Input::SdlInputBridge::ProcessEvent(event);

            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    Exit();
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (!event.key.repeat)
                    {
                        if (event.key.key == SDLK_F9)
                            GraphicsDevice_.GetRenderer().DebugSimulateContextLoss();
                        else if (event.key.key == SDLK_F10)
                            GraphicsDevice_.GetRenderer().DebugRestoreContext();
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    Window_.updateFromSDL();
                    break;

                case SDL_EVENT_WILL_ENTER_BACKGROUND:
                    setIsActiveProperty(false);
                    break;

                case SDL_EVENT_DID_ENTER_FOREGROUND:
                    setIsActiveProperty(true);
                    break;

                // Desktop focus switch (e.g. Alt-Tab), as opposed to the mobile-style
                // background/foreground events above. Matches FNA's SDL3 platform loop, which sets
                // game.IsActive = false/true on these same two events
                // (SDL3_FNAPlatform.cs:1006-1037). Keyboard/mouse state is intentionally NOT
                // cleared here — see DEC-15 in docs/input-fna-fidelity.md.
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    setIsActiveProperty(false);
                    break;

                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    setIsActiveProperty(true);
                    break;

                default:
                    break;
            }
        }
    }

    void Game::OnComponentAdded(System::Object* sender, const GameComponentCollectionEventArgs& args)
    {
        (void) sender;

        IGameComponent* component = args.getGameComponentProperty();
        if (component != nullptr)
        {
            component->Initialize();
            CategorizeComponent(component);
        }
    }

    void Game::OnComponentRemoved(System::Object* sender, const GameComponentCollectionEventArgs& args)
    {
        (void) sender;

        IGameComponent* component = args.getGameComponentProperty();
        if (component == nullptr)
        {
            return;
        }

        if (auto* updateable = dynamic_cast<IUpdateable*>(component))
        {
            RemoveUpdateable(updateableComponents_, updateable);
            const auto it = updateOrderChangedTokens_.find(updateable);
            if (it != updateOrderChangedTokens_.end())
            {
                updateable->getUpdateOrderChangedEvent().Remove(it->second);
                updateOrderChangedTokens_.erase(it);
            }
        }

        if (auto* drawable = dynamic_cast<IDrawable*>(component))
        {
            RemoveDrawable(drawableComponents_, drawable);
            const auto it = drawOrderChangedTokens_.find(drawable);
            if (it != drawOrderChangedTokens_.end())
            {
                drawable->getDrawOrderChangedEvent().Remove(it->second);
                drawOrderChangedTokens_.erase(it);
            }
        }
    }

    void Game::OnUpdateOrderChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void) args;

        if (auto* updateable = dynamic_cast<IUpdateable*>(sender))
        {
            SortUpdateable(updateable);
        }
    }

    void Game::OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void) args;

        if (auto* drawable = dynamic_cast<IDrawable*>(sender))
        {
            SortDrawable(drawable);
        }
    }

}
