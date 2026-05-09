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

#ifdef __ANDROID__
        // Trap the Android Back button so it arrives as SDL_EVENT_KEY_DOWN/UP
        // (SDLK_AC_BACK) instead of being silently converted to SDL_EVENT_QUIT.
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
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
        // Pass the current virtual resolution (XNA-layer state) to the backend.
        // The authoritative source is GraphicsDeviceManager::PreferredBackBufferWidth/Height
        // propagated via ApplyChanges() -> SetVirtualResolution(). At construction time
        // virtualWidth_/virtualHeight_ are 0 (unspecified); the SDL backend handles 0
        // by deferring logical presentation until SetVirtualResolution() is called.
        args.virtualWidth  = virtualWidth_;
        args.virtualHeight = virtualHeight_;
        SDL_Log("[WindowDebug] Backend virtual size: width=%d height=%d", args.virtualWidth, args.virtualHeight);
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
        int width = 0;
        int height = 0;

        if (backend_) {
            backend_->GetViewportSize(width, height);
        }

        if (width <= 0 || height <= 0) {
            SDL_Log("[Viewport] Skipping zero-size viewport update (w=%d h=%d); keeping previous w=%d h=%d",
                    width, height,
                    Viewport_.getWidthProperty(), Viewport_.getHeightProperty());
            return;
        }

        // Avoid log spam: only log and update when size actually changes.
        if (width == Viewport_.getWidthProperty() && height == Viewport_.getHeightProperty()) {
            return;
        }

        Viewport_.x = 0;
        Viewport_.y = 0;
        Viewport_.minDepth = 0.0f;
        Viewport_.maxDepth = 1.0f;
        Viewport_.setWidthProperty(width);
        Viewport_.setHeightProperty(height);
        SDL_Log("[Viewport] Updated viewport: x=0 y=0 w=%d h=%d", width, height);
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
            // Present first; the backend may re-apply logical presentation
            // (e.g. on Android surface resize), then read the updated viewport.
            backend_->Present();
            UpdateViewportFromWindow();
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

    void GraphicsDevice::SetVertexBuffer(const VertexBuffer* vertexBuffer)
    {
        currentVertexBuffer_ = vertexBuffer;
    }

    void GraphicsDevice::SetIndexBuffer(const IndexBuffer* indexBuffer)
    {
        currentIndexBuffer_ = indexBuffer;
    }

    void GraphicsDevice::DrawPrimitives(PrimitiveType primitiveType,
                                        int vertexStart,
                                        int primitiveCount)
    {
        if (!backend_) return;
        if (!currentVertexBuffer_) {
            throw std::runtime_error(
                "GraphicsDevice::DrawPrimitives: no vertex buffer is bound. "
                "Call SetVertexBuffer(...) before DrawPrimitives.");
        }
        if (!currentEffect_) {
            throw std::runtime_error(
                "GraphicsDevice::DrawPrimitives: no effect applied. "
                "Call BasicEffect::Apply() before DrawPrimitives.");
        }
        if (vertexStart != 0) {
            throw std::runtime_error(
                "GraphicsDevice::DrawPrimitives: non-zero vertexStart is not "
                "yet supported by the CNA backend.");
        }
        backend_->DrawColoredPrimitives(
            currentVertexBuffer_->GetBackend(),
            currentEffect_->World, currentEffect_->View, currentEffect_->Projection,
            primitiveType, primitiveCount
        );
    }

    void GraphicsDevice::DrawIndexedPrimitives(PrimitiveType primitiveType,
                                               int baseVertex,
                                               int /*minVertexIndex*/,
                                               int /*numVertices*/,
                                               int startIndex,
                                               int primitiveCount)
    {
        if (!backend_) return;
        if (!currentVertexBuffer_) {
            throw std::runtime_error(
                "GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound. "
                "Call SetVertexBuffer(...) before DrawIndexedPrimitives.");
        }
        if (!currentIndexBuffer_) {
            throw std::runtime_error(
                "GraphicsDevice::DrawIndexedPrimitives: no index buffer is bound. "
                "Call SetIndexBuffer(...) before DrawIndexedPrimitives.");
        }
        if (!currentEffect_) {
            throw std::runtime_error(
                "GraphicsDevice::DrawIndexedPrimitives: no effect applied. "
                "Call BasicEffect::Apply() before DrawIndexedPrimitives.");
        }
        if (baseVertex != 0) {
            throw std::runtime_error(
                "GraphicsDevice::DrawIndexedPrimitives: non-zero 'baseVertex' "
                "is not yet supported by the CNA backend (PARTIAL).");
        }
        if (startIndex != 0) {
            throw std::runtime_error(
                "GraphicsDevice::DrawIndexedPrimitives: non-zero 'startIndex' "
                "is not yet supported by the CNA backend (PARTIAL).");
        }
        backend_->DrawIndexedColoredPrimitives(
            currentVertexBuffer_->GetBackend(),
            currentIndexBuffer_->GetBackend(),
            currentEffect_->World, currentEffect_->View, currentEffect_->Projection,
            primitiveType, primitiveCount
        );
    }

    void GraphicsDevice::DrawUserPrimitives(PrimitiveType /*primitiveType*/,
                                            const void* /*vertexData*/,
                                            int /*vertexOffset*/,
                                            int /*primitiveCount*/)
    {
        throw std::runtime_error(
            "GraphicsDevice::DrawUserPrimitives is not implemented yet "
            "in the CNA 3D subset (STUB). Use SetVertexBuffer + "
            "DrawPrimitives instead.");
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType /*primitiveType*/,
                                                   const void* /*vertexData*/,
                                                   int /*vertexOffset*/,
                                                   int /*numVertices*/,
                                                   const void* /*indexData*/,
                                                   int /*indexOffset*/,
                                                   int /*primitiveCount*/)
    {
        throw std::runtime_error(
            "GraphicsDevice::DrawUserIndexedPrimitives is not implemented "
            "yet in the CNA 3D subset (STUB). Use SetVertexBuffer + "
            "SetIndexBuffer + DrawIndexedPrimitives instead.");
    }

    SDL_Renderer* GraphicsDevice::GetRendererInternal() const
    {
        return backend_ ? backend_->GetRendererInternal() : nullptr;
    }

    SDL_Window* GraphicsDevice::GetWindowInternal() const
    {
        return backend_ ? backend_->GetWindowInternal() : nullptr;
    }

    void GraphicsDevice::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0) {
            SDL_Log("[Viewport] SetVirtualResolution: ignoring invalid size %dx%d", width, height);
            return;
        }
        virtualWidth_  = width;
        virtualHeight_ = height;
        SDL_Log("[Viewport] SetVirtualResolution: %dx%d", width, height);
        if (backend_) {
            backend_->SetVirtualResolution(width, height);
        }
        UpdateViewportFromWindow();
    }

    void GraphicsDevice::SetPresentationMode(int mode)
    {
        SDL_Log("[Presentation] GraphicsDevice::SetPresentationMode: mode=%d", mode);
        if (backend_) {
            backend_->SetPresentationMode(mode);
        }
    }
}
