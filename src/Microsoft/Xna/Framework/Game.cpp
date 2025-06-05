#include "Microsoft/Xna/Framework/Game.h"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace Microsoft::Xna::Framework {
    const int TARGET_FPS = 20;
    const double FRAME_TIME = 1.0 / TARGET_FPS;

    igetter(Content::ContentManager, Content, Game)
    igetter(Graphics::GraphicsDevice, GraphicsDevice, Game)
    idata(bool, IsMouseVisible, Game)
    idata(System::TimeSpan, TargetElapsedTime, Game)
    idata(System::TimeSpan, InactiveSleepTime, Game)

    Game::Game() : TargetElapsedTime_(TimeSpan(0)), InactiveSleepTime_(TimeSpan(0)), isRunning(true) {
    }

    Game::~Game() {
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


        GameTime gameTime;
        gameTime.setElapsedGameTimeProperty(System::TimeSpan::FromMilliseconds(50));
        double msPerFrame = 1000 / TARGET_FPS;

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

            Uint64 frameTime = SDL_GetTicks() - frameStart;
            Uint64 frameDelay = msPerFrame;
            std::cout << "frameTime=" << frameTime << std::endl;
            std::cout << "frameDelay=" << frameDelay << std::endl;
            std::cout << "frameDelay - frameTime=" << (frameDelay - frameTime) << std::endl;


            //            if (frameDelay > frameTime)
            //            {
            //                SDL_Delay(frameDelay - frameTime);
            //            }

            i++;
            //std::cout<<"next frame"<<i;
        }
    }

    void Game::Initialize() {
        // Initialize game resources
        LoadContent();
    }

    void Game::LoadContent() {
        // Loading content (e.g. textures)
    }

    void Game::OnDeactivated(std::any sender, System::Runtime::CompilerServices::EventArgs args) {
    }

    void Game::OnActivated(std::any sender, System::Runtime::CompilerServices::EventArgs args) {
    }

    void Game::Update(const Microsoft::Xna::Framework::GameTime &gameTime) {
        // Main game logic
    }

    void Game::Draw(const Microsoft::Xna::Framework::GameTime &gameTime) {
    }
}
