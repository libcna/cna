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

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::Viewport, Viewport, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDevice, nothing)

    GraphicsDevice::GraphicsDevice()
        : impl_(std::make_unique<Impl>()),
          Viewport_()
    {
        std::cout << "Starting GraphicsDevice()" << std::endl;

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            throw std::runtime_error(
                std::string("SDL video subsystem initialization failed: ") + SDL_GetError()
            );
        }

        impl_->window = SDL_CreateWindow("CNA Game", 800, 600, SDL_WINDOW_RESIZABLE);
        if (!impl_->window) {
            throw std::runtime_error(
                std::string("SDL_CreateWindow failed: ") + SDL_GetError()
            );
        }

        impl_->renderer = SDL_CreateRenderer(impl_->window, nullptr);
        if (!impl_->renderer) {
            SDL_DestroyWindow(impl_->window);
            impl_->window = nullptr;

            throw std::runtime_error(
                std::string("SDL_CreateRenderer failed: ") + SDL_GetError()
            );
        }

        if (!SDL_SetRenderVSync(impl_->renderer, 1)) {
            std::cerr << "Warning: SDL_SetRenderVSync failed: " << SDL_GetError() << std::endl;
        }



        const char* name = SDL_GetRendererName(GetRendererInternal());

        if (!name) {
            SDL_Log("SDL_GetRendererName failed: %s", SDL_GetError());
        } else if (SDL_strcmp(name, "opengl") == 0) {
            SDL_Log("SDL_Renderer uses OpenGL");
        } else if (SDL_strcmp(name, "gpu") == 0) {
            SDL_GPUDevice* device = SDL_GetGPURendererDevice(GetRendererInternal());
            if (device) {
                const char* gpuDriver = SDL_GetGPUDeviceDriver(device);
                SDL_Log("SDL_Renderer = gpu, current backend = %s",
                        gpuDriver ? gpuDriver : "unknown");
            } else {
                SDL_Log("Renderer is gpu, but GPU device could not be find out: %s", SDL_GetError());
            }
        } else {
            SDL_Log("Other SDL_Renderer backend: %s", name);
        }



        UpdateViewportFromWindow();
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

        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void GraphicsDevice::UpdateViewportFromWindow()
    {
        int width = 800;
        int height = 600;

        if (impl_ && impl_->window) {
            SDL_GetWindowSize(impl_->window, &width, &height);
        }

        Viewport_.x = 0;
        Viewport_.y = 0;
        Viewport_.minDepth = 0.0f;
        Viewport_.maxDepth = 1.0f;
        Viewport_.setWidthProperty(width);
        Viewport_.setHeightProperty(height);
    }

    void GraphicsDevice::Clear(const Color& color)
    {
        Clear(
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        );
    }

    void GraphicsDevice::Clear(float r, float g, float b, float a)
    {
        SDL_Renderer* renderer = GetRendererInternal();
        if (!renderer) {
            throw std::runtime_error("GraphicsDevice::Clear failed: renderer is null.");
        }

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
        SDL_Renderer* renderer = GetRendererInternal();
        if (!renderer) {
            throw std::runtime_error("GraphicsDevice::Present failed: renderer is null.");
        }

        UpdateViewportFromWindow();
        SDL_RenderPresent(renderer);
    }

    SDL_Renderer* GraphicsDevice::GetRendererInternal() const
    {
        return impl_ ? impl_->renderer : nullptr;
    }

    SDL_Window* GraphicsDevice::GetWindowInternal() const
    {
        return impl_ ? impl_->window : nullptr;
    }
}