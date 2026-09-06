// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Game.hpp"

#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/TargetPlatform.hpp"
#include "System/Globalization/CultureInfo.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>

EM_ASYNC_JS(void, CNA_WaitForAnimationFrame, (), {
    await new Promise((resolve) => requestAnimationFrame(resolve));
});
#endif

namespace Microsoft::Xna::Framework
{
    struct Game::PlatformEventBatch
    {
        std::vector<CNA::Platform::PlatformEvent> events;
    };

#if defined(__EMSCRIPTEN__)
    // Defined here rather than next to EmscriptenMainLoopCallback below because ~Game() reads it,
    // and a nested type has to be complete by then.
    struct Game::EmscriptenLoopState
    {
        Game* game = nullptr;
        GameTime gameTime;
        std::uint64_t lastTickMs = 0;
        double accumulatorMs = 0.0;
    };

    Game::EmscriptenLoopState Game::s_emLoopState;
#endif

    namespace
    {
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

        /// Reads the currently installed platform without triggering the lazy creation
        /// GetCurrentPlatform() performs when nothing is installed.
        [[nodiscard]] CNA::Platform::IPlatform* InstalledPlatformOrNull()
        {
            return CNA::Platform::HasCurrentPlatform() ? &CNA::Platform::GetCurrentPlatform()
                                                       : nullptr;
        }

        void InitializeDefaultCulture(CNA::Platform::IPlatform& platform)
        {
            using System::Globalization::CultureInfo;

            const bool needsCulture =
                !CultureInfo::getDefaultThreadCurrentCultureProperty().has_value();
            const bool needsUiCulture =
                !CultureInfo::getDefaultThreadCurrentUICultureProperty().has_value();
            if (!needsCulture && !needsUiCulture)
            {
                return;
            }

            for (const CNA::Platform::PlatformLocale& locale :
                 platform.GetSystemInfo()->GetPreferredLocales())
            {
                if (locale.language.empty())
                {
                    continue;
                }

                std::string name = locale.language;
                if (!locale.country.empty())
                {
                    name += "-" + locale.country;
                }

                try
                {
                    const CultureInfo preferred(name);
                    if (needsCulture)
                    {
                        CultureInfo::setDefaultThreadCurrentCultureProperty(preferred);
                    }
                    if (needsUiCulture)
                    {
                        CultureInfo::setDefaultThreadCurrentUICultureProperty(preferred);
                    }
                    return;
                }
                catch (const System::Globalization::CultureNotFoundException&)
                {
                    // A platform can report a locale newer than the runtime's accepted syntax.
                    // Keep walking the ordered preference list instead of discarding all choices.
                }
            }
        }

        // Live games, in the order they installed themselves; the back entry is the one currently
        // aimed at by the ambient accessor.
        //
        // A per-game "the platform I displaced" pointer looks simpler and is wrong: games are not
        // guaranteed to be destroyed in reverse construction order, and a game that saved a
        // predecessor which is destroyed first would restore a dangling pointer on its own way
        // out. Keeping the order in one place lets a game remove itself from the middle, which is
        // the case that actually goes wrong.
        std::mutex& PlatformStackMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::vector<CNA::Platform::IPlatform*>& PlatformStack()
        {
            static std::vector<CNA::Platform::IPlatform*> stack;
            return stack;
        }

        /// Installs the game's owned platform as the process-wide one in a single step.
        ///
        /// Doing both from a member initialiser rather than from the constructor body is what
        /// makes the ordering guarantee real: every other member is constructed after this one,
        /// so a graphics device or content manager that reaches for the ambient platform during
        /// its own construction finds the game's instance rather than lazily creating a second.
        [[nodiscard]] std::unique_ptr<CNA::Platform::IPlatform> InstallPlatform(
            std::unique_ptr<CNA::Platform::IPlatform> platform)
        {
            if (!platform)
            {
                throw std::invalid_argument("Game requires a non-null platform");
            }

            {
                const std::lock_guard<std::mutex> guard(PlatformStackMutex());
                PlatformStack().push_back(platform.get());
            }
            CNA::Platform::SetCurrentPlatform(platform.get());
            return platform;
        }

        /// Removes a game's platform from the stack and re-aims the ambient accessor at whatever
        /// is left, which may be nothing.
        void UninstallPlatform(CNA::Platform::IPlatform* platform)
        {
            CNA::Platform::IPlatform* successor = nullptr;
            {
                const std::lock_guard<std::mutex> guard(PlatformStackMutex());
                std::vector<CNA::Platform::IPlatform*>& stack = PlatformStack();
                const auto found = std::find(stack.rbegin(), stack.rend(), platform);
                if (found != stack.rend())
                {
                    stack.erase(std::next(found).base());
                }
                successor = stack.empty() ? nullptr : stack.back();
            }

            // Only re-aim if this game is the one currently aimed at. Something outside Game may
            // have installed its own platform (tests do), and a game going away is no reason to
            // take that over.
            if (InstalledPlatformOrNull() == platform)
            {
                CNA::Platform::SetCurrentPlatform(successor);
            }
        }
    }

    /// PLAT-46 follow-up: the scope guard that makes the ambient installation transactional.
    ///
    /// Armed the moment the platform is installed and disarmed once Game's constructor completes.
    /// In between, a throw from any later member destroys this object -- members already
    /// constructed are destroyed during unwinding, even though ~Game never runs -- and the
    /// installation is undone before the platform itself goes away with the argument temporary.
    struct Game::PlatformInstallation
    {
        explicit PlatformInstallation(CNA::Platform::IPlatform* installed) : platform(installed) {}

        ~PlatformInstallation()
        {
            if (platform != nullptr)
            {
                UninstallPlatform(platform);
            }
        }

        PlatformInstallation(const PlatformInstallation&)            = delete;
        PlatformInstallation& operator=(const PlatformInstallation&) = delete;

        /// Called once the constructor has completed: from then on ~Game owns the uninstall.
        void Disarm() { platform = nullptr; }

        CNA::Platform::IPlatform* platform = nullptr;
    };

    const System::TimeSpan Game::MaxElapsedTime = System::TimeSpan::FromMilliseconds(500.0);

    const std::string& Game::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Game";
        return typeName;
    }

    Game::Game()
        : Game(CNA::Platform::PlatformFactory::Create())
    {
    }

    Game::Game(std::unique_ptr<CNA::Platform::IPlatform> platform)
        : platform_(InstallPlatform(std::move(platform))),
          platformInstallation_(std::make_unique<PlatformInstallation>(platform_.get())),
          platformCapabilities_(platform_->GetCapabilities()),
          eventBatch_(std::make_unique<PlatformEventBatch>()),
          Components_(),
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
          isSuspended_(false),
          gameTime_(),
          previousPerformanceCounter_(0),
          accumulatedElapsedTime_(System::TimeSpan::Zero),
          updateFrameLag_(0),
          previousSleepTimes_(),
          sleepTimeIndex_(0),
          worstCaseSleepPrecision_(System::TimeSpan::FromMilliseconds(1.0)),
          RunApplication(true)
    {
        InitializeDefaultCulture(*platform_);

        for (auto& previousSleepTime : previousSleepTimes_)
        {
            previousSleepTime = System::TimeSpan::FromMilliseconds(1.0);
        }

        Window_.setWindowInternal(
            GraphicsDevice_.GetPlatformWindowInternal(),
            GraphicsDevice_.GetWindowHandleInternal());
        Content_.setGraphicsDevice(GraphicsDevice_);

        // A real Game instance is the framework startup boundary. Keep ContentManager neutral for
        // isolated use, but make every XNA game ready to load built-in XNB types before
        // Initialize()/LoadContent() can run.
        CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();

        FrameworkDispatcher::Update();

        // Construction completed: from here the destructor is guaranteed to run, and it is the one
        // that uninstalls. Leaving the guard armed would uninstall twice -- harmless today, but it
        // would also move the uninstall after the body, quietly changing the ordering ~Game's own
        // comment relies on.
        platformInstallation_->Disarm();
    }

    Game::~Game()
    {
#if defined(__EMSCRIPTEN__)
        // Destruction from a re-entrant callback while this game owns the browser loop must stop
        // RunLoop before its platform and graphics resources disappear.
        if (s_emLoopState.game == this)
        {
            s_emLoopState.game = nullptr;
            CNA::Logger::Error(
                "CNA: the Game driving the Emscripten loop was destroyed while Run() was active; "
                "the loop stops before its resources are released.");
        }
#endif
        Dispose(false);
        // Hand the ambient installation back to whichever game is still alive, if any. This runs
        // before platform_ is destroyed (members are destroyed after the body), so the accessor
        // is never left aimed at a platform that has already gone away.
        UninstallPlatform(platform_.get());
    }

    CNA::Platform::IPlatform& Game::GetPlatformEXT() const
    {
        return *platform_;
    }

    const CNA::Platform::PlatformCapabilities& Game::GetPlatformCapabilitiesEXT() const
    {
        return platformCapabilities_;
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

        // Still gated on there being a window: cursor visibility is meaningless without one, and
        // the platform's mouse service is null when the platform has no pointer at all (headless,
        // and eventually terminal), which is a second reason the same call can be a no-op.
        if (GraphicsDevice_.GetPlatformWindowInternal() != nullptr)
        {
            if (CNA::Platform::IPlatformMouse* mouse = platform_->GetMouse())
            {
                mouse->SetCursorVisible(value);
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
            previousPerformanceCounter_ = platform_->GetPerformanceCounter();
            hasInitialized_ = true;
        }

        Tick();
    }

    void Game::Run()
    {
        // EmscriptenMainLoopCallback catches what a FRAME throws, but everything before the loop --
        // Initialize, LoadContent, the first device and content work -- had no reporting at all. On
        // the web that is the difference between a diagnosis and a blank canvas: an exception that
        // escapes Run() reaches the browser as a rejected promise carrying a bare `{excPtr}`, with
        // no type, no what() and nothing in the console, and Emscripten exposes no helper to read
        // it back. Log it where the message still exists and let it keep unwinding, so this changes
        // what a developer is told and nothing else. A native build reaches std::terminate, which
        // prints what() itself, so this only ever adds a line there.
        try
        {
            AssertNotDisposed();

            if (!hasInitialized_)
            {
                DoInitialize();
                hasInitialized_ = true;
            }

            BeginRun();
            BeforeLoop();

            previousPerformanceCounter_ = platform_->GetPerformanceCounter();
            RunLoop();

            EndRun();
            AfterLoop();
        }
        catch (const std::exception& exception)
        {
            CNA::Logger::Error(
                std::string("CNA: fatal exception escaped Game::Run(): ") + exception.what());
            throw;
        }
        catch (...)
        {
            CNA::Logger::Error("CNA: a fatal exception that is not a std::exception escaped "
                               "Game::Run().");
            throw;
        }
    }

    void Game::Tick()
    {
        AdvanceElapsedTime();

        if (IsFixedTimeStep_)
        {
            while (TimeSpanLess(accumulatedElapsedTime_ + worstCaseSleepPrecision_, TargetElapsedTime_))
            {
                platform_->Delay(1);
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
            int stepCount = 0;

            while (TimeSpanGreaterOrEqual(accumulatedElapsedTime_, TargetElapsedTime_))
            {
                // XNA 4.0's own clock, measured against the real runtime rather than inherited
                // from FNA: the game's FIRST update runs with a zero ElapsedGameTime, and
                // TotalGameTime is the time BEFORE the step rather than after it, so it advances
                // once Update returns. FNA sets ElapsedGameTime = TargetElapsedTime for every
                // update including the first and advances TotalGameTime before calling Update
                // (FNA/src/Game.cs:475), which leaves a game two fixed steps ahead of XNA's by
                // the same update index -- visible in any simulation whose state accumulates.
                gameTime_.setElapsedGameTimeProperty(
                    hasUpdatedOnce_ ? TargetElapsedTime_ : System::TimeSpan::Zero);
                accumulatedElapsedTime_ = accumulatedElapsedTime_ - TargetElapsedTime_;
                ++stepCount;

                AssertNotDisposed();
                Update(gameTime_);

                gameTime_.setTotalGameTimeProperty(
                    gameTime_.getTotalGameTimeProperty() + gameTime_.getElapsedGameTimeProperty());
                hasUpdatedOnce_ = true;
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
            }

            accumulatedElapsedTime_ = System::TimeSpan::Zero;
            AssertNotDisposed();
            Update(gameTime_);

            // Same rule as the fixed path: TotalGameTime is the time before this step. Measured
            // on the real XNA runtime with IsFixedTimeStep = false, where update 3 reports
            // elapsed=0.0211 with total still 0 and update 4 reports total=0.0211.
            gameTime_.setTotalGameTimeProperty(
                gameTime_.getTotalGameTimeProperty() + gameTime_.getElapsedGameTimeProperty());
            hasUpdatedOnce_ = true;
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
        {
            // A background loading thread may be adding components right now; take the snapshot
            // under the lock and release it before running any component's own Draw().
            const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);
            currentlyDrawingComponents_.insert(
                currentlyDrawingComponents_.end(),
                drawableComponents_.begin(),
                drawableComponents_.end()
            );
        }

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
        {
            const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);
            currentlyUpdatingComponents_.insert(
                currentlyUpdatingComponents_.end(),
                updateableComponents_.begin(),
                updateableComponents_.end()
            );
        }

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

        }

        // Nothing to release for controllers: Game no longer acquires that subsystem (see
        // DoInitialize()), and the platform releases whatever it acquired on its own behalf when
        // it is destroyed -- which, since Game owns the platform, is a moment later than this.

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

        // No controller subsystem is acquired here. plans/plan_platform.md PLAT-83 did, to make
        // already-connected pads visible in frame one, and the cost turned out to be a full udev
        // device enumeration -- ~1.9 seconds on this project's Linux reference machine, paid
        // before the first frame by every game whether or not it ever reads a controller, and seen
        // by a player as a window that stays blank for two seconds. The platform now acquires it
        // when something first asks for the gamepad or joystick service, so a game that wants
        // controllers still gets them and one that does not pays nothing.

        Initialize();

        {
            const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);
            updateableComponents_.clear();
            drawableComponents_.clear();
        }

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

        // Also covers the two token maps below: a component added from a loading thread writes
        // them while a component removed on the game thread reads and erases them.
        const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);

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

        const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);
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

        const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);
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

            // The platform's epoch is its own creation rather than library init, which changes
            // nothing here: only deltas are used, and the first callback seeds lastTickMs either way.
            const std::uint64_t nowMs = state.game->GetPlatformEXT().GetTicksMilliseconds();
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
                state.game->OnExiting(state.game, System::EventArgs::Empty);
                state.game = nullptr;
            }
        }
        catch (const std::exception& exception)
        {
            s_emLoopState.game = nullptr;
            CNA::Logger::Error(
                std::string("CNA: fatal exception in Emscripten main loop: ") + exception.what());
        }
        catch (...)
        {
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

        // Asyncify suspends and resumes this same Wasm stack between frames. Calling the frame body
        // directly is important: a separately registered browser callback cannot re-enter the
        // Wasm instance while this Run() stack is suspended. Unlike simulateInfiniteLoop, this
        // preserves the logical caller and therefore the ordinary stack-local XNA Game lifetime.
        while (s_emLoopState.game == this)
        {
            EmscriptenMainLoopCallback();
            if (s_emLoopState.game == this)
                CNA_WaitForAnimationFrame();
        }
#else
        while (RunApplication)
        {
            // plans/plan_apple.md APPLE-7: a backgrounded mobile application keeps its main thread
            // alive but must neither render nor burn CPU. isMobilePlatform() is a
            // compile-time constant, so desktop builds keep the plain Tick() loop unchanged.
            if (CNA::isMobilePlatform() && isSuspended_)
            {
                WaitWhileSuspended();
                continue;
            }

            Tick();
        }

        OnExiting(this, System::EventArgs::Empty);
#endif
    }

    void Game::WaitWhileSuspended()
    {
        // Park the thread instead of spinning: while suspended there is nothing to update and
        // nothing that may be drawn. The platform contract has no blocking event wait, and
        // deliberately so -- a sleep-then-poll pair says the same thing without every
        // implementation having to grow one. The interval is short enough that the resume event,
        // which arrives on the queue polled below, is acted on within a frame, and long enough
        // that the wait costs nothing measurable.
        constexpr std::uint32_t suspendedPollIntervalMs = 16;
        platform_->Delay(suspendedPollIntervalMs);
        PollEvents();
    }

    System::TimeSpan Game::AdvanceElapsedTime()
    {
        // FNA uses System.Diagnostics.Stopwatch; CNA uses the platform's high-resolution
        // monotonic counter for the equivalent.
        const std::uint64_t currentCounter = platform_->GetPerformanceCounter();

        if (previousPerformanceCounter_ == 0)
        {
            previousPerformanceCounter_ = currentCounter;
            return System::TimeSpan::Zero;
        }

        const std::uint64_t frequency = platform_->GetPerformanceFrequency();
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
        std::vector<CNA::Platform::PlatformEvent>& events = eventBatch_->events;
        platform_->PollEvents(events);

        for (const CNA::Platform::PlatformEvent& event : events)
        {
            // Input observes every mapped event first. In particular, Exit() below must not stop
            // the rest of this batch from reaching the input state machine (PLAT-6 finding 4).
            CNA::Internal::Input::PlatformInputBridge::ProcessEvent(event);

            std::visit([this](const auto& platformEvent)
            {
                using Event = std::decay_t<decltype(platformEvent)>;

                if constexpr (std::is_same_v<Event, CNA::Platform::QuitEvent>)
                {
                    Exit();
                }
                else if constexpr (std::is_same_v<Event, CNA::Platform::KeyEvent>)
                {
                    if (platformEvent.pressed && !platformEvent.repeat)
                    {
                        if (platformEvent.keycode == CNA::Platform::KeyCode::F9)
                            GraphicsDevice_.GetRenderer().DebugSimulateContextLoss();
                        else if (platformEvent.keycode == CNA::Platform::KeyCode::F10)
                            GraphicsDevice_.GetRenderer().DebugRestoreContext();
                    }
                }
                else if constexpr (std::is_same_v<Event, CNA::Platform::WindowEvent>)
                {
                    // Deliberately do not filter by window id and do not consume the size payload:
                    // both details are observable legacy behaviour captured by PLAT-6.
                    switch (platformEvent.kind)
                    {
                    case CNA::Platform::WindowEventKind::Resized:
                    case CNA::Platform::WindowEventKind::PixelSizeChanged:
                        Window_.updateFromPlatform();
                        GraphicsDevice_.UpdateViewportFromWindow();
                        GraphicsDevice_.GetRenderer().OnSurfaceInvalidated(platformEvent.window);
                        break;
                    case CNA::Platform::WindowEventKind::DisplayScaleChanged:
                        Window_.updateFromPlatform();
                        GraphicsDevice_.UpdateViewportFromWindow();
                        GraphicsDevice_.GetRenderer().OnSurfaceInvalidated(platformEvent.window);
                        break;
                    case CNA::Platform::WindowEventKind::CloseRequested:
                        // FNA acts only on the quit event and relies on the windowing layer to
                        // synthesize one when the last window closes. That synthesis is
                        // conditional in every backend that offers it -- skipped while a tray is
                        // active, for a window that is not topmost, and whenever the backend's
                        // own opt-out is set -- so relying on it alone leaves a game whose close
                        // button silently does nothing. XNA's close button always ends the game,
                        // so act on the request itself. Exit() is idempotent, which is what makes
                        // the usual case, where a synthesized quit arrives in this same batch,
                        // cost nothing.
                        Exit();
                        break;
                    case CNA::Platform::WindowEventKind::FocusLost:
                        setIsActiveProperty(false);
                        break;
                    case CNA::Platform::WindowEventKind::FocusGained:
                        setIsActiveProperty(true);
                        GraphicsDevice_.GetRenderer().OnSurfaceInvalidated(platformEvent.window);
                        break;
                    case CNA::Platform::WindowEventKind::Exposed:
                    case CNA::Platform::WindowEventKind::Minimized:
                    case CNA::Platform::WindowEventKind::Maximized:
                    case CNA::Platform::WindowEventKind::Restored:
                    case CNA::Platform::WindowEventKind::DisplayChanged:
                        GraphicsDevice_.GetRenderer().OnSurfaceInvalidated(platformEvent.window);
                        break;
                    default:
                        break;
                    }
                }
                else if constexpr (std::is_same_v<Event, CNA::Platform::AppLifecycleEvent>)
                {
                    switch (platformEvent.kind)
                    {
                    // plans/plan_apple.md APPLE-7 — a deviation from FNA, which tracks only IsActive
                    // here. A mobile operating system terminates an application that submits GPU
                    // work once it is in the background (iOS), or destroys the rendering surface
                    // at that point (Android), so the loop must actually stop between these two
                    // events rather than keep drawing into a dead surface. The flag is set on
                    // every platform and acted on only where the events are raised, which is
                    // mobile.
                    case CNA::Platform::AppLifecycleKind::WillEnterBackground:
                        isSuspended_ = true;
                        setIsActiveProperty(false);
                        break;
                    case CNA::Platform::AppLifecycleKind::DidEnterForeground:
                        isSuspended_ = false;
                        // The suspension was not gameplay time. Restarting the counter makes the
                        // first frame after the resume measure only itself, instead of the whole
                        // background period clamped to MaxElapsedTime — which would run a burst of
                        // catch-up Updates before the first visible frame.
                        previousPerformanceCounter_ = 0;
                        accumulatedElapsedTime_ = System::TimeSpan::Zero;
                        ResetElapsedTime();
                        setIsActiveProperty(true);
                        GraphicsDevice_.GetRenderer().OnSurfaceInvalidated(0);
                        break;
                    // Mobile memory pressure. The game itself decides what to release (XNA has no
                    // hook for it), but the warning belongs in the log: the next step after this
                    // event is usually the operating system killing the process.
                    case CNA::Platform::AppLifecycleKind::LowMemory:
                        CNA::Logger::Warn("Game: the operating system reported low memory.");
                        break;
                    // Already decided by the operating system: this is the last event the
                    // application receives, so the loop has to end here for OnExiting to run at
                    // all. Clearing the suspended flag first is what lets Run() leave the wait
                    // loop and reach OnExiting instead of parking until the process is killed.
                    case CNA::Platform::AppLifecycleKind::Terminating:
                        isSuspended_ = false;
                        Exit();
                        break;
                    }
                }
            }, event);
        }

        // Snapshot services advance once per frame, after the native queue has been pumped and
        // before Update()/Draw() can read them. Keyboard keys/modifiers share one clock; mouse
        // absolute state/buttons share another. Relative mouse displacement remains consume-on-read
        // inside that service to preserve FNA's deliberate draining semantics.
        if (CNA::Platform::IPlatformKeyboard* keyboard = platform_->GetKeyboard())
        {
            keyboard->Update();
        }
        if (CNA::Platform::IPlatformMouse* mouse = platform_->GetMouse())
        {
            mouse->Update();
        }
        // Gated, and the gate is what makes the lazy acquisition in DoInitialize()'s comment work:
        // reaching GetGamepad()/GetJoystick() is itself what asks the platform to start the
        // controller subsystem, so an unconditional pump here would put its cost back at frame one
        // and defeat the whole thing. Once a game has genuinely asked for controllers -- through
        // GamePad or Joysticks, which go to the same accessors -- the subsystem is initialized and
        // this pump runs from that frame on.
        if (platform_->IsSubsystemInitialized(CNA::Platform::PlatformSubsystem::Gamepad))
        {
            if (CNA::Platform::IPlatformGamepad* gamepad = platform_->GetGamepad())
            {
                gamepad->Update();
            }
            if (CNA::Platform::IPlatformJoystick* joystick = platform_->GetJoystick())
            {
                joystick->Update();
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

        const std::lock_guard<std::recursive_mutex> lock(componentListsMutex_);

        if (auto* updateable = dynamic_cast<IUpdateable*>(component))
        {
            RemoveUpdateable(updateableComponents_, updateable);
            // A component removed while a frame is in flight must also disappear from that
            // frame's snapshot. XNA can leave it there because the snapshot holds a strong
            // reference and the object stays alive; these are raw pointers, and whoever removed
            // the component usually owns and frees it in the same breath -- which is exactly what
            // a screen manager does when the update it is running drops the screen that owns the
            // components. Nulling rather than erasing keeps Update()'s own iteration valid; both
            // loops already skip a null entry.
            for (IUpdateable*& queued : currentlyUpdatingComponents_)
            {
                if (queued == updateable)
                {
                    queued = nullptr;
                }
            }
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
            for (IDrawable*& queued : currentlyDrawingComponents_)
            {
                if (queued == drawable)
                {
                    queued = nullptr;
                }
            }
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
