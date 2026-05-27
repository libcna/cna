#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"

#ifdef CNA_BACKEND_BGFX
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif

#include <SDL3/SDL.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Backends::CreateGraphicsBackend;
    using CNA::Internal::Backends::GraphicsBackendCreateArgs;

    namespace
    {
        std::runtime_error makeSdlError(const char* operation)
        {
            return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
        }

        [[nodiscard]] bool hasClearFlag(ClearOptions options, ClearOptions flag)
        {
            return (static_cast<int>(options) & static_cast<int>(flag)) != 0;
        }

        void LogWindowDebugState(SDL_Window* window, const char* context)
        {
            if (window == nullptr)
            {
                SDL_Log("[WindowDebug] %s: window=null", context);
                return;
            }

            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            const bool borderless = (flags & SDL_WINDOW_BORDERLESS) != 0;
            const bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;

            SDL_Log(
                "[WindowDebug] %s: flags=0x%llx borderless=%s fullscreen=%s",
                context,
                static_cast<unsigned long long>(flags),
                borderless ? "true" : "false",
                fullscreen ? "true" : "false"
            );
        }

        [[nodiscard]] SDL_WindowFlags getBackendWindowFlags()
        {
            SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;

#ifdef CNA_BACKEND_EASYGL
            windowFlags |= SDL_WINDOW_OPENGL;
#endif

#ifdef CNA_BACKEND_VULKAN
            windowFlags |= SDL_WINDOW_VULKAN;
#endif

#ifdef CNA_BACKEND_BGFX
            const auto rendererType = CNA::Internal::Backends::Bgfx::Detail::ResolveRendererType(SDL_getenv("CNA_BGFX_RENDERER"));
            switch (rendererType)
            {
                case bgfx::RendererType::Vulkan:
                    windowFlags |= SDL_WINDOW_VULKAN;
                    break;

                case bgfx::RendererType::OpenGL:
                case bgfx::RendererType::OpenGLES:
                case bgfx::RendererType::Count:
                    windowFlags |= SDL_WINDOW_OPENGL;
                    break;

                default:
                    break;
            }
#endif

            return windowFlags;
        }
    }

    GraphicsDevice::GraphicsDevice()
        : GraphicsDevice(
            GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::Reach,
            PresentationParameters()
        )
    {
    }

    GraphicsDevice::GraphicsDevice(
        GraphicsAdapter& adapter,
        GraphicsProfile graphicsProfile,
        const PresentationParameters& presentationParameters
    )
        : window_(nullptr),
          ownsWindow_(false),
          backend_(nullptr),
          viewport_(),
          currentVertexBuffer_(nullptr),
          currentIndexBuffer_(nullptr),
          currentEffect_(nullptr),
          virtualWidth_(presentationParameters.getBackBufferWidthProperty()),
          virtualHeight_(presentationParameters.getBackBufferHeightProperty()),
          adapter_(&adapter),
          graphicsProfile_(graphicsProfile),
          presentationParameters_(presentationParameters),
          isDisposed_(false)
    {
#ifdef __ANDROID__
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            throw makeSdlError("SDL_InitSubSystem(SDL_INIT_VIDEO)");
        }

        createOrAttachWindow();
        applyPresentationParametersToWindow();
        createBackend();
        UpdateViewportFromWindow();
    }

    GraphicsDevice::~GraphicsDevice()
    {
        Dispose();
    }

    GraphicsAdapter& GraphicsDevice::getAdapterProperty() const
    {
        return adapter_ != nullptr ? *adapter_ : GraphicsAdapter::getDefaultAdapterProperty();
    }

    GraphicsProfile GraphicsDevice::getGraphicsProfileProperty() const
    {
        return graphicsProfile_;
    }

    PresentationParameters& GraphicsDevice::getPresentationParametersProperty()
    {
        return presentationParameters_;
    }

    const PresentationParameters& GraphicsDevice::getPresentationParametersProperty() const
    {
        return presentationParameters_;
    }

    const Viewport& GraphicsDevice::getViewportProperty() const
    {
        return viewport_;
    }

    void GraphicsDevice::setViewportProperty(const Viewport& value)
    {
        viewport_ = value;
    }

    const IndexBuffer* GraphicsDevice::getIndicesProperty() const
    {
        return currentIndexBuffer_;
    }

    void GraphicsDevice::setIndicesProperty(const IndexBuffer* indexBuffer)
    {
        SetIndexBuffer(indexBuffer);
    }

    bool GraphicsDevice::getIsDisposedProperty() const
    {
        return isDisposed_;
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
        if (backend_ != nullptr)
        {
            backend_->Clear(r, g, b, a);
        }
    }

    void GraphicsDevice::Clear(ClearOptions options, const Color& color, float depth, int stencil)
    {
        (void) stencil;

        if (backend_ == nullptr)
        {
            return;
        }

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const bool clearTarget = hasClearFlag(options, ClearOptions::Target);
        const bool clearDepth = hasClearFlag(options, ClearOptions::DepthBuffer);

        if (clearTarget && clearDepth)
        {
            backend_->ClearColorAndDepth(r, g, b, a, depth);
        }
        else if (clearTarget)
        {
            backend_->Clear(r, g, b, a);
        }
        else if (clearDepth)
        {
            // Backend has no depth-only clear hook yet. Use existing color+depth
            // path while preserving the current color argument.
            backend_->ClearColorAndDepth(r, g, b, a, depth);
        }
    }

    void GraphicsDevice::Clear(const Color& color, float depth)
    {
        Clear(ClearOptions::Target | ClearOptions::DepthBuffer, color, depth, 0);
    }

    void GraphicsDevice::Present()
    {
        if (backend_ != nullptr)
        {
            backend_->Present();
            UpdateViewportFromWindow();
        }
    }

    void GraphicsDevice::Reset(const PresentationParameters& presentationParameters, GraphicsAdapter& adapter)
    {
        Reset(presentationParameters, &adapter);
    }

    void GraphicsDevice::Reset(const PresentationParameters& presentationParameters, GraphicsAdapter* adapter)
    {
        DeviceResetting.Raise(this, System::EventArgs::Empty);

        presentationParameters_ = presentationParameters;
        if (adapter != nullptr)
        {
            adapter_ = adapter;
        }

        virtualWidth_ = presentationParameters_.getBackBufferWidthProperty();
        virtualHeight_ = presentationParameters_.getBackBufferHeightProperty();

        applyPresentationParametersToWindow();

        if (backend_ != nullptr)
        {
            backend_->SetVirtualResolution(virtualWidth_, virtualHeight_);
        }

        UpdateViewportFromWindow();
        DeviceReset.Raise(this, System::EventArgs::Empty);
    }

    void GraphicsDevice::Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        Disposing.Raise(this, System::EventArgs::Empty);
        destroyNativeResources();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        isDisposed_ = true;
    }

    void GraphicsDevice::SetDepthTestEnabled(bool enabled)
    {
        if (backend_ != nullptr)
        {
            backend_->SetDepthTestEnabled(enabled);
        }
    }

    void GraphicsDevice::SetVertexBuffer(const VertexBuffer* vertexBuffer)
    {
        currentVertexBuffer_ = vertexBuffer;
    }

    void GraphicsDevice::SetIndexBuffer(const IndexBuffer* indexBuffer)
    {
        currentIndexBuffer_ = indexBuffer;
    }

    const VertexBuffer* GraphicsDevice::GetVertexBuffer() const
    {
        return currentVertexBuffer_;
    }

    const IndexBuffer* GraphicsDevice::GetIndexBuffer() const
    {
        return currentIndexBuffer_;
    }

    const IndexBuffer* GraphicsDevice::Indices() const
    {
        return currentIndexBuffer_;
    }

    void GraphicsDevice::Indices(const IndexBuffer* indexBuffer)
    {
        SetIndexBuffer(indexBuffer);
    }

    void GraphicsDevice::DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount)
    {
        if (backend_ == nullptr)
        {
            return;
        }

        if (currentVertexBuffer_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: no vertex buffer is bound.");
        }

        if (currentEffect_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: no effect has been applied.");
        }

        if (vertexStart != 0)
        {
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: non-zero vertexStart is not supported by the current backend.");
        }

        backend_->DrawColoredPrimitives(
            currentVertexBuffer_->GetBackend(),
            currentEffect_->World,
            currentEffect_->View,
            currentEffect_->Projection,
            primitiveType,
            primitiveCount
        );
    }

    void GraphicsDevice::DrawIndexedPrimitives(
        PrimitiveType primitiveType,
        int baseVertex,
        int minVertexIndex,
        int numVertices,
        int startIndex,
        int primitiveCount
    ) {
        (void) minVertexIndex;
        (void) numVertices;

        if (backend_ == nullptr)
        {
            return;
        }

        if (currentVertexBuffer_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound.");
        }

        if (currentIndexBuffer_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no index buffer is bound.");
        }

        if (currentEffect_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no effect has been applied.");
        }

        if (baseVertex != 0)
        {
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: non-zero baseVertex is not supported by the current backend.");
        }

        if (startIndex != 0)
        {
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: non-zero startIndex is not supported by the current backend.");
        }

        backend_->DrawIndexedColoredPrimitives(
            currentVertexBuffer_->GetBackend(),
            currentIndexBuffer_->GetBackend(),
            currentEffect_->World,
            currentEffect_->View,
            currentEffect_->Projection,
            primitiveType,
            primitiveCount
        );
    }

    void GraphicsDevice::DrawUserPrimitives(
        PrimitiveType primitiveType,
        const void* vertexData,
        int vertexOffset,
        int primitiveCount
    ) {
        (void) primitiveType;
        (void) vertexData;
        (void) vertexOffset;
        (void) primitiveCount;

        throw std::runtime_error(
            "GraphicsDevice::DrawUserPrimitives is not implemented yet by the current CNA backend."
        );
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType primitiveType,
        const void* vertexData,
        int vertexOffset,
        int numVertices,
        const void* indexData,
        int indexOffset,
        int primitiveCount
    ) {
        (void) primitiveType;
        (void) vertexData;
        (void) vertexOffset;
        (void) numVertices;
        (void) indexData;
        (void) indexOffset;
        (void) primitiveCount;

        throw std::runtime_error(
            "GraphicsDevice::DrawUserIndexedPrimitives is not implemented yet by the current CNA backend."
        );
    }

    CNA::Internal::Backends::IGraphicsBackend& GraphicsDevice::GetBackend() const
    {
        if (backend_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice backend is not available.");
        }

        return *backend_;
    }

    void GraphicsDevice::SetCurrentEffect(BasicEffect* effect)
    {
        currentEffect_ = effect;
    }

    const std::string& GraphicsDevice::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.GraphicsDevice";
        return typeName;
    }

    SDL_Renderer* GraphicsDevice::GetRendererInternal() const
    {
        return backend_ != nullptr ? backend_->GetRendererInternal() : nullptr;
    }

    SDL_Window* GraphicsDevice::GetWindowInternal() const
    {
        return backend_ != nullptr ? backend_->GetWindowInternal() : window_;
    }

    void GraphicsDevice::createOrAttachWindow()
    {
        const auto requestedHandle = presentationParameters_.getDeviceWindowHandleProperty();
        if (requestedHandle != 0)
        {
            window_ = reinterpret_cast<SDL_Window*>(requestedHandle);
            ownsWindow_ = false;
            return;
        }

        SDL_WindowFlags windowFlags = getBackendWindowFlags();

        const int width = presentationParameters_.getBackBufferWidthProperty() > 0 ?
            presentationParameters_.getBackBufferWidthProperty() :
            800;

        const int height = presentationParameters_.getBackBufferHeightProperty() > 0 ?
            presentationParameters_.getBackBufferHeightProperty() :
            480;

        window_ = SDL_CreateWindow("Game", width, height, windowFlags);
        if (window_ == nullptr)
        {
            throw makeSdlError("SDL_CreateWindow");
        }

        ownsWindow_ = true;
        presentationParameters_.setDeviceWindowHandleProperty(reinterpret_cast<PresentationParameters::IntPtr>(window_));

        LogWindowDebugState(window_, "after SDL_CreateWindow");
    }

    void GraphicsDevice::createBackend()
    {
        GraphicsBackendCreateArgs args;
        args.window = window_;
        args.virtualWidth = virtualWidth_;
        args.virtualHeight = virtualHeight_;

        backend_ = CreateGraphicsBackend(args);

        if (backend_ != nullptr)
        {
            backend_->SetVirtualResolution(virtualWidth_, virtualHeight_);
        }
    }

    void GraphicsDevice::destroyNativeResources()
    {
        backend_.reset();

        if (window_ != nullptr && ownsWindow_)
        {
            SDL_DestroyWindow(window_);
        }

        window_ = nullptr;
        ownsWindow_ = false;
    }

    void GraphicsDevice::UpdateViewportFromWindow()
    {
        int width = 0;
        int height = 0;

        if (backend_ != nullptr)
        {
            backend_->GetViewportSize(width, height);
        }

        if ((width <= 0 || height <= 0) && window_ != nullptr)
        {
            SDL_GetWindowSize(window_, &width, &height);
        }

        if (width <= 0 || height <= 0)
        {
            return;
        }

        if (width == viewport_.getWidthProperty() && height == viewport_.getHeightProperty())
        {
            return;
        }

        viewport_.x = 0;
        viewport_.y = 0;
        viewport_.minDepth = 0.0f;
        viewport_.maxDepth = 1.0f;
        viewport_.setWidthProperty(width);
        viewport_.setHeightProperty(height);
    }

    void GraphicsDevice::SetVirtualResolution(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        virtualWidth_ = width;
        virtualHeight_ = height;

        presentationParameters_.setBackBufferWidthProperty(width);
        presentationParameters_.setBackBufferHeightProperty(height);

        if (backend_ != nullptr)
        {
            backend_->SetVirtualResolution(width, height);
        }

        UpdateViewportFromWindow();
    }

    void GraphicsDevice::SetPresentationMode(int mode)
    {
        if (backend_ != nullptr)
        {
            backend_->SetPresentationMode(mode);
        }
    }

    void GraphicsDevice::applyPresentationParametersToWindow()
    {
        if (window_ == nullptr)
        {
            return;
        }

        const bool fullScreen = presentationParameters_.getIsFullScreenProperty();
        if (!SDL_SetWindowFullscreen(window_, fullScreen))
        {
            throw makeSdlError("SDL_SetWindowFullscreen");
        }

        const int width = presentationParameters_.getBackBufferWidthProperty();
        const int height = presentationParameters_.getBackBufferHeightProperty();
        if (width > 0 && height > 0)
        {
#ifndef __ANDROID__

            if (!SDL_SetWindowSize(window_, width, height))
            {
                throw makeSdlError("SDL_SetWindowSize");
            }
#endif
        }
    }
}
