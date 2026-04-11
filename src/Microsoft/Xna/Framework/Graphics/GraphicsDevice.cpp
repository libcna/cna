#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include <iostream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics {

    GraphicsDevice::GraphicsDevice()
        : Viewport_()
    {
        std::cout << "Starting GraphicsDevice()" << std::endl;

        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL3 could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
            throw std::runtime_error(std::string("SDL3 could not initialize! SDL_Error: ") + SDL_GetError());
        }

        window = SDL_CreateWindow("CNA Game", 800, 600, SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            throw std::runtime_error(std::string("Window could not be created! SDL_Error: ") + SDL_GetError());
        }

        renderer = SDL_CreateRenderer(window, "vulkan");
        if (!renderer) {
            std::cerr << "Vulkan renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            renderer = SDL_CreateRenderer(window, nullptr);
            if (!renderer) {
                std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
                throw std::runtime_error(std::string("Renderer could not be created! SDL_Error: ") + SDL_GetError());
            }
        }

        if (!SDL_SetRenderVSync(renderer, 1)) {
            std::cerr << "Warning: SDL_SetRenderVSync failed: " << SDL_GetError() << std::endl;
        }
    }

    GraphicsDevice::~GraphicsDevice()
    {
        std::cout << "Calling ~GraphicsDevice()" << std::endl;

        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }

        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }

        SDL_Quit();
    }

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::Viewport, Viewport, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDevice, nothing)

    void GraphicsDevice::Clear(const Color& color)
    {
    }

    void GraphicsDevice::Clear(float r, float g, float b, float a)
    {
        SDL_SetRenderDrawColor(
            renderer,
            static_cast<Uint8>(r * 255.0f),
            static_cast<Uint8>(g * 255.0f),
            static_cast<Uint8>(b * 255.0f),
            static_cast<Uint8>(a * 255.0f)
        );
        SDL_RenderClear(renderer);
    }

    void GraphicsDevice::Present()
    {
        SDL_RenderPresent(renderer);
    }

    SDL_Renderer* GraphicsDevice::GetRenderer()
    {
        return renderer;
    }
}