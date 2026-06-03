#include "Microsoft/Xna/Framework/Game.hpp"

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Logger.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

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

    Game::Game()
        : Components_(),
          Content_(),
          GraphicsDevice_(),
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

        Window_.setWindowInternal(GraphicsDevice_.GetBackend().GetWindowInternal());
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
            graphicsDeviceService_ = Services_.GetService<IGraphicsDeviceService>();
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

            gameTime_.setElapsedGameTimeProperty(
                System::TimeSpan::FromMilliseconds(
                    TotalMilliseconds(TargetElapsedTime_) * static_cast<double>(stepCount)
                )
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

        graphicsDeviceService_ = Services_.GetService<IGraphicsDeviceService>();
        if (graphicsDeviceService_ == nullptr || graphicsDeviceService_->getGraphicsDeviceProperty() != nullptr)
        {
            LoadContent();
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

    void Game::OnExiting(System::Object* sender, const ExitingEventArgs& args)
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

        Initialize();

        updateableComponents_.clear();
        drawableComponents_.clear();

        for (SharpRuntime::intcs i = 0; i < Components_.getCountProperty(); ++i)
        {
            CategorizeComponent(Components_[i]);
        }
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
        }

        if (auto* drawable = dynamic_cast<IDrawable*>(component))
        {
            SortDrawable(drawable);
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

        ExitingEventArgs exitingEventArgs;
        OnExiting(this, exitingEventArgs);
#endif
    }

    System::TimeSpan Game::AdvanceElapsedTime()
    {
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
                            GraphicsDevice_.GetBackend().DebugSimulateContextLoss();
                        else if (event.key.key == SDLK_F10)
                            GraphicsDevice_.GetBackend().DebugRestoreContext();
                    }
                    break;

                case SDL_EVENT_WILL_ENTER_BACKGROUND:
                    setIsActiveProperty(false);
                    break;

                case SDL_EVENT_DID_ENTER_FOREGROUND:
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
        }

        if (auto* drawable = dynamic_cast<IDrawable*>(component))
        {
            RemoveDrawable(drawableComponents_, drawable);
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
            ExitingEventArgs exitingEventArgs;
            state.game->OnExiting(state.game, exitingEventArgs);
        }
    }
#endif
}
