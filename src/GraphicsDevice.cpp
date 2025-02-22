#include "CNA/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace CNA {

    GraphicsDevice::GraphicsDevice() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL2 could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        }

        window = SDL_CreateWindow("CNA Game", 800, 600, SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        }

        renderer = SDL_CreateRenderer(window, NULL);
        if (!renderer) {
            std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        }
    }

    GraphicsDevice::~GraphicsDevice() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void GraphicsDevice::Clear(float r, float g, float b, float a) {
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(r * 255), static_cast<Uint8>(g * 255), static_cast<Uint8>(b * 255), static_cast<Uint8>(a * 255));
        SDL_RenderClear(renderer);
    }

    void GraphicsDevice::Present() {
        SDL_RenderPresent(renderer);
    }
    SDL_Renderer* GraphicsDevice::GetRenderer() {
        return renderer;
    }
}
