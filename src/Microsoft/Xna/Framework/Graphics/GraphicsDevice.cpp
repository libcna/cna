#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <SDL3/SDL.h>

#include <iostream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics {

    using namespace CNA::Internal::Backends;

    IMPL_PROP(Microsoft::Xna::Framework::Graphics::Viewport, Viewport, getter1, setter0, member0, static0, constret0, ref1, constmet0, GraphicsDevice, nothing)

    GraphicsDevice::GraphicsDevice()
        : window_(nullptr),
          backend_(nullptr),
          Viewport_()
    {
        std::cout << "Starting GraphicsDevice()" << std::endl;

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL video subsystem initialization failed: ") + SDL_GetError());
        }

        uint32_t window_flags = SDL_WINDOW_RESIZABLE;
#ifdef CNA_BACKEND_EASYGL
        window_flags |= SDL_WINDOW_OPENGL;
#endif
#ifdef CNA_BACKEND_VULKAN
        window_flags |= SDL_WINDOW_VULKAN;
#endif

        window_ = SDL_CreateWindow("CNA Game", 800, 600, window_flags);
        if (!window_) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        GraphicsBackendCreateArgs args;
        args.window = window_;
        backend_ = CreateGraphicsBackend(args);
        UpdateViewportFromWindow();
    }

    GraphicsDevice::~GraphicsDevice()
    {
        std::cout << "Calling ~GraphicsDevice()" << std::endl;
        backend_.reset();
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void GraphicsDevice::UpdateViewportFromWindow()
    {
        int width = 800;
        int height = 600;

        if (backend_) {
            backend_->GetViewportSize(width, height);
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
        if (backend_) {
            backend_->Clear(r, g, b, a);
        }
    }

    void GraphicsDevice::Present()
    {
        if (backend_) {
            UpdateViewportFromWindow();
            backend_->Present();
        }
    }

    SDL_Renderer* GraphicsDevice::GetRendererInternal() const
    {
        return backend_ ? backend_->GetRendererInternal() : nullptr;
    }

    SDL_Window* GraphicsDevice::GetWindowInternal() const
    {
        return backend_ ? backend_->GetWindowInternal() : nullptr;
    }
}