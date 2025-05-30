#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <iostream>

namespace Microsoft::Xna::Framework::Graphics {

    GraphicsDevice::GraphicsDevice():
    Viewport([this]() { return viewport; }) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL2 could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        }

        window = SDL_CreateWindow("CNA Game", 800, 600, SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        }

        // Try creating Vulkan renderer
        renderer = SDL_CreateRenderer(window, "vulkan");
        if (!renderer) {
            std::cerr << "Vulkan renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            // Optionally, you can try another renderer (OpenGL, default)
            renderer = SDL_CreateRenderer(window, nullptr); // Use default renderer
            if (!renderer) {
                std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            }
        }

        // Enable V-Sync (Double buffering should be automatic with Vulkan)
        //SDL_SetRenderVSync(renderer, SDL_RENDERER_VSYNC_ADAPTIVE);
    }


    GraphicsDevice::~GraphicsDevice() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void GraphicsDevice::Clear(const Color &color) {
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
