#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Logger.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <iostream>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace Microsoft::Xna::Framework
{
    IMPL_PROP(Content::ContentManager, Content, getter1, setter0, member0, static0, constret0, ref1, constmet0, Game,
              nothing)

    Graphics::GraphicsDevice& Game::getGraphicsDeviceProperty()
    {
        return GraphicsDevice_;
    }

    GameWindow& Game::getWindowProperty()
    {
        return Window_;
    }

    IMPL_PROP(bool, IsMouseVisible, getter1, setter1, member0, static0, constret1, ref1, constmet1, Game, nothing)

    const System::TimeSpan& Game::getTargetElapsedTimeProperty() const
    {
        return TargetElapsedTime_;
    }

    void Game::setTargetElapsedTimeProperty(const System::TimeSpan& v)
    {
        TargetElapsedTime_ = v;
    }

    IMPL_PROP(System::TimeSpan, InactiveSleepTime, getter1, setter1, member0, static0, constret1, ref1, constmet1, Game,
              nothing)

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

    Game::Game()
        : IsMouseVisible_(false),
          TargetElapsedTime_(TimeSpan(500000L)),
          InactiveSleepTime_(TimeSpan(0)),
          isRunning(true)
    {
        Window_.setWindowInternal(GraphicsDevice_.GetBackend().GetWindowInternal());
        Content_.setGraphicsDevice(GraphicsDevice_);
        InitAudio();
    }

    Game::~Game()
    {
        std::cout << "Calling ~Game()" << std::endl;
        ShutdownAudio();
    }

#if defined(__EMSCRIPTEN__)
    // Per-frame state shared with the Emscripten main-loop callback.
    //
    // Timing model: the browser fires the callback via requestAnimationFrame
    // (typically ~60 fps).  Inside the callback we accumulate the real elapsed
    // wall-clock time and call Update() only for each full TargetElapsedTime
    // slice, then Draw() once.  This keeps gameplay speed identical to the
    // native SDL_Delay loop which also fires Update() every TargetElapsedTime.
    //
    // We cap the single-frame delta at 250 ms to avoid a runaway catch-up
    // burst when the browser tab was hidden for a while.
    struct Game::EmscriptenLoopState
    {
        Game* game = nullptr;
        GameTime gameTime;
        Uint64 lastTickMs = 0; // SDL_GetTicks() at end of previous callback
        double accumulatorMs = 0.0; // accumulated unprocessed milliseconds
    };

    Game::EmscriptenLoopState Game::s_emLoopState;

    void Game::EmscriptenMainLoopCallback()
    {
        EmscriptenLoopState& s = s_emLoopState;

        // --- pump events ---
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            CNA::Internal::Input::SdlInputBridge::ProcessEvent(e);
            if (e.type == SDL_EVENT_QUIT)
            {
                s.game->isRunning = false;
                emscripten_cancel_main_loop();
                ExitingEventArgs exiting_event_args;
                s.game->Exiting.Raise(s.game, exiting_event_args);
                return;
            }
            else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
            {
                if (e.key.key == SDLK_F9)
                    s.game->GraphicsDevice_.GetBackend().DebugSimulateContextLoss();
                else if (e.key.key == SDLK_F10)
                    s.game->GraphicsDevice_.GetBackend().DebugRestoreContext();
            }
        }

        // --- accumulate real elapsed time ---
        const Uint64 nowMs = SDL_GetTicks();
        if (s.lastTickMs == 0)
        {
            // First callback: treat as one target-step so the game starts immediately.
            s.lastTickMs = nowMs;
        }
        double deltaMs = static_cast<double>(nowMs - s.lastTickMs);
        s.lastTickMs = nowMs;

        // Cap to 250 ms to prevent spiral-of-death after tab was hidden.
        if (deltaMs > 250.0) deltaMs = 250.0;
        s.accumulatorMs += deltaMs;

        const double targetMs = s.game->getTargetMsFrameTimeProperty();
        const auto stepSpan = System::TimeSpan::FromMilliseconds(targetMs);

        // --- fire Update() for each full target-step in the accumulator ---
        bool updated = false;
        while (s.accumulatorMs >= targetMs)
        {
            s.accumulatorMs -= targetMs;

            s.gameTime.setElapsedGameTimeProperty(stepSpan);
            s.gameTime.setTotalGameTimeProperty(
                s.gameTime.getTotalGameTimeProperty() + stepSpan);
            s.gameTime.setIsRunningSlowlyProperty(false);

            s.game->Update(s.gameTime);
            updated = true;
        }

        // --- Draw once per browser frame (regardless of update count) ---
        if (updated)
        {
            s.game->Draw(s.gameTime);
            s.game->getGraphicsDeviceProperty().Present();
        }
    }
#endif // __EMSCRIPTEN__

    void Game::Run()
    {
        const int compiled = SDL_VERSION;
        const int linked = SDL_GetVersion();

        SDL_Log("We compiled against SDL version %d.%d.%d ...\n",
                SDL_VERSIONNUM_MAJOR(compiled),
                SDL_VERSIONNUM_MINOR(compiled),
                SDL_VERSIONNUM_MICRO(compiled));

        SDL_Log("But we are linking against SDL version %d.%d.%d.\n",
                SDL_VERSIONNUM_MAJOR(linked),
                SDL_VERSIONNUM_MINOR(linked),
                SDL_VERSIONNUM_MICRO(linked));

        const char* envDriver = SDL_getenv("SDL_VIDEODRIVER");
        const char* currentDriver = SDL_GetCurrentVideoDriver();
        SDL_Log("SDL_VIDEODRIVER=%s; current SDL video driver=%s",
                envDriver ? envDriver : "(null)",
                currentDriver ? currentDriver : "(null)");

        CNA::Logger::Info("SpeedyBlupi: before Initialize()");
        Initialize();
        CNA::Logger::Info("SpeedyBlupi: after Initialize(), entering game loop");

        const double wantedMsFrameTime = getTargetMsFrameTimeProperty();

        GameTime gameTime{};
        gameTime.setElapsedGameTimeProperty(TimeSpan::FromMilliseconds(wantedMsFrameTime));
        gameTime.setIsRunningSlowlyProperty(false);

#if defined(__EMSCRIPTEN__)
        // Browser: hand control back to the JS event loop via emscripten_set_main_loop.
        // A blocking while-loop would freeze the browser tab.
        //
        // fps=0 → the browser drives the callback via requestAnimationFrame (~60 fps).
        // Correct game speed is maintained by the fixed-timestep accumulator inside
        // EmscriptenMainLoopCallback(): Update() fires only when >= TargetElapsedTime
        // has accumulated, so it runs at exactly 20 fps (= 1000/50ms) regardless of
        // the browser frame rate.
        s_emLoopState.game = this;
        s_emLoopState.gameTime = gameTime;
        emscripten_set_main_loop(EmscriptenMainLoopCallback, 0, 1);
#else
        // isAppPaused tracks Android background/foreground state.
        // When paused we stop rendering and sleep to avoid busy-looping.
        bool isAppPaused = false;

        while (isRunning)
        {
            const Uint64 frameStart = SDL_GetTicks();

            SDL_Event e;
            while (SDL_PollEvent(&e))
            {
                CNA::Internal::Input::SdlInputBridge::ProcessEvent(e);
                if (e.type == SDL_EVENT_QUIT)
                {
                    isRunning = false;
                }
                else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
                {
                    if (e.key.key == SDLK_F9)
                        GraphicsDevice_.GetBackend().DebugSimulateContextLoss();
                    else if (e.key.key == SDLK_F10)
                        GraphicsDevice_.GetBackend().DebugRestoreContext();
                }
                else if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND)
                {
                    // Android / mobile: app is being sent to background.
                    // SDL3 handles audio focus automatically at the platform layer.
                    // We stop rendering and signal deactivation to the game.
                    isAppPaused = true;
                    System::EventArgs deactivated_args;
                    OnDeactivated(this, deactivated_args);
                }
                else if (e.type == SDL_EVENT_DID_ENTER_FOREGROUND)
                {
                    // Android / mobile: app returned to foreground.
                    isAppPaused = false;
                    System::EventArgs activated_args;
                    OnActivated(this, activated_args);
                }
            }

            if (isAppPaused)
            {
                // Do not render while backgrounded; sleep to avoid CPU busy-loop.
                SDL_Delay(16);
                continue;
            }

            Update(gameTime);
            Draw(gameTime);
            GraphicsDevice_.Present();

            const Uint64 workMs = SDL_GetTicks() - frameStart;
            const bool runningSlowly = static_cast<double>(workMs) > wantedMsFrameTime;

            if (!runningSlowly)
            {
                const double remainingMs = wantedMsFrameTime - static_cast<double>(workMs);
                if (remainingMs > 0.0)
                {
                    SDL_Delay(static_cast<Uint32>(remainingMs));
                }
            }

            const Uint64 fullFrameMs = SDL_GetTicks() - frameStart;

            const auto elapsed = System::TimeSpan::FromMilliseconds(static_cast<double>(fullFrameMs));
            gameTime.setElapsedGameTimeProperty(elapsed);
            gameTime.setTotalGameTimeProperty(gameTime.getTotalGameTimeProperty() + elapsed);
            gameTime.setIsRunningSlowlyProperty(runningSlowly);
        }

        CNA::Logger::Info("SpeedyBlupi: game loop exited, raising Exiting event");
        ExitingEventArgs exiting_event_args;
        Exiting.Raise(this, exiting_event_args);
        CNA::Logger::Info("SpeedyBlupi: Run() completed");
#endif // __EMSCRIPTEN__
    }

    void Game::Initialize()
    {
        CNA::Logger::Info("SpeedyBlupi: Game::Initialize entered");
        LoadContent();
        CNA::Logger::Info("SpeedyBlupi: Game::Initialize done (LoadContent returned)");
    }

    void Game::LoadContent()
    {
    }

    void Game::UnloadContent()
    {
    }

    void Game::OnDeactivated(std::any sender, System::EventArgs args)
    {
    }

    void Game::OnActivated(std::any sender, System::EventArgs args)
    {
    }

    void Game::Update(Microsoft::Xna::Framework::GameTime& gameTime)
    {
    }

    void Game::Draw(const Microsoft::Xna::Framework::GameTime& gameTime)
    {
    }

#ifdef XNA5
    double Game::getTargetFPSProperty() const
    {
        const double msPerFrame = getTargetMsFrameTimeProperty();
        if (msPerFrame <= 0.0)
        {
            return 0.0;
        }
        return 1000.0 / msPerFrame;
    }

    double Game::getTargetMsFrameTimeProperty() const
    {
        return getTargetElapsedTimeProperty().getTotalMillisecondsProperty();
    }

    double Game::fpsToMillisecondsPerFrame(const SharpRuntime::intcs framesPerSecond)
    {
        if (framesPerSecond <= 0)
        {
            return 0.0;
        }
        return 1000.0 / static_cast<double>(framesPerSecond);
    }
#endif
}
