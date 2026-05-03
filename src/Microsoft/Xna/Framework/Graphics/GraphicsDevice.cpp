#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#ifdef CNA_BACKEND_BGFX
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif
#include <SDL3/SDL.h>

#include <iostream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics {

    using namespace CNA::Internal::Backends;

    namespace
    {
        void LogWindowDebugState(SDL_Window* window, const char* context)
        {
            if (!window) {
                SDL_Log("[WindowDebug] %s: window=null", context);
                return;
            }
            const Uint64 flags = SDL_GetWindowFlags(window);
            const bool borderless = (flags & SDL_WINDOW_BORDERLESS) != 0;
            const bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
            int top = 0;
            int left = 0;
            int bottom = 0;
            int right = 0;
            const bool bordersSizeOk = SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
            const char* driver = SDL_GetCurrentVideoDriver();
            SDL_Log(
                "[WindowDebug] %s: driver=%s flags=0x%llx borderless=%s bordered=%s fullscreen=%s borders_size_ok=%s borders=(t:%d l:%d b:%d r:%d)",
                context,
                driver ? driver : "(null)",
                static_cast<unsigned long long>(flags),
                borderless ? "true" : "false",
                borderless ? "false" : "true",
                fullscreen ? "true" : "false",
                bordersSizeOk ? "true" : "false",
                top,
                left,
                bottom,
                right
            );
        }
    }

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
        const char* envDriver = SDL_getenv("SDL_VIDEODRIVER");
        const char* activeDriverAfterInit = SDL_GetCurrentVideoDriver();
        SDL_Log(
            "[WindowDebug] SDL_VIDEODRIVER=%s active_video_driver_after_init=%s",
            envDriver ? envDriver : "(null)",
            activeDriverAfterInit ? activeDriverAfterInit : "(null)"
        );

        uint32_t window_flags = SDL_WINDOW_RESIZABLE;
#ifdef CNA_BACKEND_EASYGL
        window_flags |= SDL_WINDOW_OPENGL;
#endif
#ifdef CNA_BACKEND_VULKAN
        window_flags |= SDL_WINDOW_VULKAN;
#endif
#ifdef CNA_BACKEND_BGFX
        const auto rendererType = CNA::Internal::Backends::Bgfx::Detail::ResolveRendererType(SDL_getenv("CNA_BGFX_RENDERER"));
        switch (rendererType) {
            case bgfx::RendererType::Vulkan:
                window_flags |= SDL_WINDOW_VULKAN;
                break;
            case bgfx::RendererType::OpenGL:
            case bgfx::RendererType::OpenGLES:
            case bgfx::RendererType::Count:
                window_flags |= SDL_WINDOW_OPENGL;
                break;
            default:
                break;
        }
#endif

        SDL_Log(
            "[WindowDebug] Creating window with flags=0x%llx (borderless_requested=%s)",
            static_cast<unsigned long long>(window_flags),
            (window_flags & SDL_WINDOW_BORDERLESS) != 0 ? "true" : "false"
        );
        window_ = SDL_CreateWindow("Game", 800, 480, window_flags);
        if (!window_) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        LogWindowDebugState(window_, "after SDL_CreateWindow");

        SDL_Log("[WindowDebug] Calling SDL_SetWindowBordered(window, true)");
        if (!SDL_SetWindowBordered(window_, true)) {
            SDL_Log("[WindowDebug] SDL_SetWindowBordered(true) failed: %s", SDL_GetError());
        } else {
            SDL_Log("[WindowDebug] SDL_SetWindowBordered(true) succeeded");
        }
        LogWindowDebugState(window_, "after SDL_SetWindowBordered(true)");

        SDL_Log("[WindowDebug] Calling SDL_SetWindowResizable(window, true)");
        SDL_SetWindowResizable(window_, true);
        LogWindowDebugState(window_, "after SDL_SetWindowResizable(true)");

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

    void GraphicsDevice::Clear(const Color& color, float depth)
    {
        if (!backend_) return;
        backend_->ClearColorAndDepth(
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f,
            depth
        );
    }

    void GraphicsDevice::SetDepthTestEnabled(bool enabled)
    {
        if (backend_) backend_->SetDepthTestEnabled(enabled);
    }

    void GraphicsDevice::DrawPrimitives(BasicEffect& effect,
                                        const VertexBuffer& vertexBuffer,
                                        PrimitiveType primitiveType,
                                        int primitiveCount)
    {
        if (!backend_) return;
        backend_->DrawColoredPrimitives(
            vertexBuffer.GetBackend(),
            effect.World, effect.View, effect.Projection,
            primitiveType, primitiveCount
        );
    }

    void GraphicsDevice::DrawIndexedPrimitives(BasicEffect& effect,
                                               const VertexBuffer& vertexBuffer,
                                               const IndexBuffer& indexBuffer,
                                               PrimitiveType primitiveType,
                                               int primitiveCount)
    {
        if (!backend_) return;
        backend_->DrawIndexedColoredPrimitives(
            vertexBuffer.GetBackend(),
            indexBuffer.GetBackend(),
            effect.World, effect.View, effect.Projection,
            primitiveType, primitiveCount
        );
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
