#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include <iostream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice::Impl {
    public:
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
    };

    GraphicsDevice::GraphicsDevice()
        : impl_(std::make_unique<Impl>()),
          Viewport_()
    {
        std::cout << "Starting GraphicsDevice()" << std::endl;

        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                throw std::runtime_error(std::string("SDL video initialization failed: ") + SDL_GetError());
            }
        }

        impl_->window = SDL_CreateWindow("CNA Game", 800, 600, SDL_WINDOW_RESIZABLE);
        if (!impl_->window) {
            throw std::runtime_error(std::string("Window creation failed: ") + SDL_GetError());
        }

        impl_->renderer = SDL_CreateRenderer(impl_->window, nullptr);
        if (!impl_->renderer) {
            SDL_DestroyWindow(impl_->window);
            impl_->window = nullptr;
            throw std::runtime_error(std::string("Renderer creation failed: ") + SDL_GetError());
        }

        if (!SDL_SetRenderVSync(impl_->renderer, 1)) {
            std::cerr << "Warning: SDL_SetRenderVSync failed: " << SDL_GetError() << std::endl;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(impl_->window, &width, &height);
        Viewport_.setWidthProperty(width);
        Viewport_.setHeightProperty(height);
    }

    GraphicsDevice::~GraphicsDevice()
    {
        std::cout << "Calling ~GraphicsDevice()" << std::endl;

        if (impl_) {
            if (impl_->renderer) {
                SDL_DestroyRenderer(impl_->renderer);
                impl_->renderer = nullptr;
            }

            if (impl_->window) {
                SDL_DestroyWindow(impl_->window);
                impl_->window = nullptr;
            }
        }

        if (SDL_WasInit(SDL_INIT_VIDEO)) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::Viewport, Viewport, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDevice, nothing)

    void GraphicsDevice::Clear(const Color& color)
    {
        Clear(
            color.getRProperty(),
            color.getGProperty(),
            color.getBProperty(),
            color.getAProperty()
        );
    }

    void GraphicsDevice::Clear(float r, float g, float b, float a)
    {
        SDL_SetRenderDrawColor(
            impl_->renderer,
            static_cast<Uint8>(r * 255.0f),
            static_cast<Uint8>(g * 255.0f),
            static_cast<Uint8>(b * 255.0f),
            static_cast<Uint8>(a * 255.0f)
        );
        SDL_RenderClear(impl_->renderer);
    }

    void GraphicsDevice::Present()
    {
        SDL_RenderPresent(impl_->renderer);
    }

    SDL_Renderer* GraphicsDevice::GetRendererInternal() const
    {
        return impl_ ? impl_->renderer : nullptr;
    }
}