#include "CNA/Game.h"
#include <SDL3/SDL.h>
#include <iostream>


namespace CNA {

    Game::Game() : isRunning(true) {
    }

    Game::~Game() {
    }

    void Game::Run() {
        Initialize();
        int i = 0;
        while (isRunning) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    isRunning = false;
                }
            }

            Update(0.016f);
            Draw();
            i++;
            std::cout<<"next frame"<<i;
            SDL_Delay(50);

        }
    }

    void Game::Initialize() {
        // Initialize game resources
        LoadContent();
    }

    void Game::LoadContent() {
        // Loading content (e.g. textures)
    }

    void Game::Update(float deltaTime) {
        // Main game logic
    }

    void Game::Draw() {

    }
}
