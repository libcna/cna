#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <iostream>
#include <stdexcept>

namespace Microsoft::Xna::Framework {

    IMPL_PROP(Content::ContentManager, Content, getter1, setter0, member0, static0, constret0, ref1, constmet0, Game, nothing)

    Graphics::GraphicsDevice& Game::getGraphicsDeviceProperty()
    {
        return GraphicsDevice_;
    }

    IMPL_PROP(bool, IsMouseVisible, getter1, setter1, member0, static0, constret1, ref1, constmet1, Game, nothing)

    System::TimeSpan* Game::getTargetElapsedTimeProperty() const
    {
        return TargetElapsedTime_;
    }

    void Game::setTargetElapsedTimeProperty(System::TimeSpan* v)
    {
        TargetElapsedTime_ = v;
    }

    IMPL_PROP(System::TimeSpan, InactiveSleepTime, getter1, setter1, member0, static0, constret1, ref1, constmet1, Game, nothing)

    void InitAudio()
    {
#ifdef SOUND_ENABLED
        if (!MIX_Init()) {
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
          TargetElapsedTime_(new TimeSpan(500000L)),
          InactiveSleepTime_(TimeSpan(0)),
          isRunning(true)
    {
        Content_.setGraphicsDevice(&GraphicsDevice_);
        InitAudio();
    }

    Game::~Game()
    {
        std::cout << "Calling ~Game()" << std::endl;
        delete TargetElapsedTime_;
        ShutdownAudio();
    }

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

        Initialize();

        double wantedMsFrameTime = getTargetMsFrameTimeProperty();

        GameTime gameTime{};
        gameTime.setElapsedGameTimeProperty(TimeSpan::FromMilliseconds(wantedMsFrameTime));
        gameTime.setIsRunningSlowlyProperty(false);

        while (isRunning) {
            const Uint64 frameStart = SDL_GetTicks();

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    isRunning = false;
                }
            }

            Update(gameTime);
            Draw(gameTime);

            const Uint64 workMs = SDL_GetTicks() - frameStart;
            const bool runningSlowly = static_cast<double>(workMs) > wantedMsFrameTime;

            if (!runningSlowly && false) {
                const double remainingMs = wantedMsFrameTime - static_cast<double>(workMs);
                if (remainingMs > 0.0) {
                    SDL_Delay(static_cast<Uint32>(remainingMs));
                }
            }

            const Uint64 fullFrameMs = SDL_GetTicks() - frameStart;

            gameTime.setElapsedGameTimeProperty(
                System::TimeSpan::FromMilliseconds(static_cast<double>(fullFrameMs))
            );
            gameTime.setIsRunningSlowlyProperty(runningSlowly);

            wantedMsFrameTime = getTargetMsFrameTimeProperty();
        }

        ExitingEventArgs exiting_event_args;
        Exiting.Raise(this, exiting_event_args);
    }

    void Game::Initialize()
    {
        LoadContent();
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
        if (msPerFrame <= 0.0) {
            return 0.0;
        }
        return 1000.0 / msPerFrame;
    }

    double Game::getTargetMsFrameTimeProperty() const
    {
        return getTargetElapsedTimeProperty()->getTotalMillisecondsProperty();
    }

    double Game::fpsToMillisecondsPerFrame(const CNA::intcs framesPerSecond)
    {
        if (framesPerSecond <= 0) {
            return 0.0;
        }
        return 1000.0 / static_cast<double>(framesPerSecond);
    }
#endif
}