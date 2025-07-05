#include "Microsoft/Xna/Framework/Game.h"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace Microsoft::Xna::Framework {

    IMPL_PROP(Content::ContentManager, Content, getter1, setter0, member0, static0, constret1, ref1, constmet1, Game, nothing)

    Graphics::GraphicsDevice& Game::getGraphicsDeviceProperty() { return GraphicsDevice_ ; }

    IMPL_PROP(bool, IsMouseVisible, getter1, setter1, member0, static0, constret1, ref1, constmet1, Game, nothing)

    System::TimeSpan* Game::getTargetElapsedTimeProperty() const { return TargetElapsedTime_; }
    void Game::setTargetElapsedTimeProperty(System::TimeSpan* v) { TargetElapsedTime_ = v; }
    IMPL_PROP(System::TimeSpan, InactiveSleepTime, getter1, setter1, member0, static0, constret1, ref1, constmet1, Game, nothing)

    Game::Game() : IsMouseVisible_(false), TargetElapsedTime_(new TimeSpan(500000L)),
                   InactiveSleepTime_(TimeSpan(0)),
                   isRunning(true) {
    }

    Game::~Game() {
        std::cout << "Calling ~Game()" << std::endl;
        delete TargetElapsedTime_;
    }

    void Game::Run() {
        const int compiled = SDL_VERSION; /* hardcoded number from SDL headers */
        const int linked = SDL_GetVersion(); /* reported by linked SDL library */

        SDL_Log("We compiled against SDL version %d.%d.%d ...\n",
                SDL_VERSIONNUM_MAJOR(compiled),
                SDL_VERSIONNUM_MINOR(compiled),
                SDL_VERSIONNUM_MICRO(compiled));

        SDL_Log("But we are linking against SDL version %d.%d.%d.\n",
                SDL_VERSIONNUM_MAJOR(linked),
                SDL_VERSIONNUM_MINOR(linked),
                SDL_VERSIONNUM_MICRO(linked));

        Initialize();
        int i = 0;

        double wantedMsFrameTime = getTargetMsFrameTimeProperty();

        GameTime gameTime {};

        auto elapsedGameTimeTimeSpan = TimeSpan::FromMilliseconds(wantedMsFrameTime);;

        gameTime.setElapsedGameTimeProperty(elapsedGameTimeTimeSpan);

        while (isRunning) {
            Uint64 frameStart = SDL_GetTicks();

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    isRunning = false;
                }
            }

            Update(gameTime);
            Draw(gameTime);

            Uint64 currentMsFrameTime = SDL_GetTicks() - frameStart;
            std::cout << "currentMsFrameTime=" << currentMsFrameTime << std::endl;
            std::cout << "wantedMsFrameTime=" << wantedMsFrameTime << std::endl;
            std::cout << "wantedMsFrameTime - currentMsFrameTime = " << (wantedMsFrameTime - currentMsFrameTime) << std::endl;

            bool runningSlowly = wantedMsFrameTime < currentMsFrameTime;
            if (!runningSlowly)
            {
                SDL_Delay(wantedMsFrameTime - currentMsFrameTime);
            }
            TimeSpan newElapsedGameTime = System::TimeSpan::FromMilliseconds(currentMsFrameTime);
            gameTime.setElapsedGameTimeProperty(newElapsedGameTime);
            gameTime.setIsRunningSlowlyProperty(runningSlowly);
            wantedMsFrameTime = getTargetMsFrameTimeProperty();

            i++;
            //std::cout<<"next frame"<<i;
            //return;
        }
        ExitingEventArgs exiting_event_args;
        Exiting.RaiseCurrentValueChanged(exiting_event_args);
    }

    void Game::Initialize() {
        // Initialize game resources
        LoadContent();
    }

    void Game::LoadContent() {
        // Loading content (e.g. textures)
    }

    void Game::UnloadContent() {
    }

    void Game::OnDeactivated(std::any sender, System::Runtime::CompilerServices::EventArgs args) {
    }

    void Game::OnActivated(std::any sender, System::Runtime::CompilerServices::EventArgs args) {
    }

    void Game::Update(Microsoft::Xna::Framework::GameTime &gameTime) {
        // Main game logic
    }

    void Game::Draw(const Microsoft::Xna::Framework::GameTime &gameTime) {
    }

#ifdef XNA5
    double Game::getTargetFPSProperty() const {
        return getTargetMsFrameTimeProperty() / 1.0;
    }

    double Game::getTargetMsFrameTimeProperty() const {
        return getTargetElapsedTimeProperty()->getTotalMillisecondsProperty();
    }

    double Game::fpsToMillisecondsPerFrame(const CNA::intcs framesPerSecond) {
        return 1.0f/double(framesPerSecond);
    }
#endif
}
