// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#ifdef CNA_BACKEND_BGFX
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Backends::CreateGraphicsBackend;
    using CNA::Internal::Backends::GraphicsBackendCreateArgs;

    namespace
    {
        // Matches FNA's internal GraphicsDevice.MAX_RENDERTARGET_BINDINGS.
        constexpr std::size_t MAX_RENDERTARGET_BINDINGS = 4;

        std::runtime_error makeSdlError(const char* operation)
        {
            return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
        }

        int toSwapInterval(PresentInterval pi)
        {
            switch (pi)
            {
                case PresentInterval::Immediate: return 0;
                case PresentInterval::Two:       return 2;
                default:                         return 1; // Default and One
            }
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
            const auto rendererType = CNA::Internal::Backends::Bgfx::Detail::ResolveRendererType(
                SDL_getenv("CNA_BGFX_RENDERER"));
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
          isDisposed_(false),
          blendState_(BlendState::Opaque),
          depthStencilState_(DepthStencilState::Default),
          rasterizerState_(RasterizerState::CullCounterClockwise),
          blendFactor_(Color::White)
    {
#ifdef __ANDROID__
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

        // plan_headless.md design decision 2 / plan_software.md design decision 4: the Headless and
        // Software backends never create a real window and never touch SDL's video subsystem at
        // all, so both can run in CI containers with no display server present -- not just a
        // headless-but-present one.
#if !defined(CNA_BACKEND_HEADLESS) && !defined(CNA_BACKEND_SOFTWARE)
        // PresentationParameters::HeadlessEXT is the runtime opt-in equivalent of the compile-time
        // guard above: a backend that normally wants a window (D3D12) can be asked for a genuinely
        // off-screen device instead. Skipping SDL_INIT_VIDEO is the point -- it is what lets such a
        // device run with no display server at all, not merely without a visible window.
        if (!presentationParameters_.getHeadlessEXTProperty())
        {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            {
                throw makeSdlError("SDL_InitSubSystem(SDL_INIT_VIDEO)");
            }
        }
#endif

        // The Touch Panel needs this for normalized-to-pixel touch coordinate scaling.
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayWidthProperty(virtualWidth_);
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayHeightProperty(virtualHeight_);

        createOrAttachWindow();
        applyPresentationParametersToWindow();
        createBackend();
        UpdateViewportFromWindow();

        // Task 896/955: blendState_/depthStencilState_/rasterizerState_ above were only ever
        // set as C++-level fields, never pushed to the backend's actual GPU state — every
        // backend started from its own hardcoded internal default (e.g. EasyGL's depth test is
        // plain OpenGL, which defaults to disabled, until something explicitly enables it) until
        // a game explicitly set one of these 3 state properties itself. Real FNA's own
        // GraphicsDevice constructor does exactly this same 3-line sync unconditionally
        // (GraphicsDevice.cs: "BlendState = BlendState.Opaque; DepthStencilState =
        // DepthStencilState.Default; RasterizerState = RasterizerState.CullCounterClockwise;") —
        // Task 896 ported only the 3rd line; this now ports the other 2 as well, matching FNA.
        setBlendStateProperty(blendState_);
        setDepthStencilStateProperty(depthStencilState_);
        setRasterizerStateProperty(rasterizerState_);
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
        if (backend_)
            backend_->SetViewport(value.getXProperty(), value.getYProperty(),
                                   value.getWidthProperty(), value.getHeightProperty(),
                                   value.getMinDepthProperty(), value.getMaxDepthProperty());
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
        // Task 928: real XNA/FNA's single-argument overload clears the target, depth buffer,
        // AND stencil together -- Clear(ClearOptions.Target | ClearOptions.DepthBuffer |
        // ClearOptions.Stencil, color, Viewport.MaxDepth, 0) -- not just the color target. The
        // depth value used is the device's own CURRENT viewport's MaxDepth (not a hardcoded 1.0),
        // matching FNA's exact `Viewport.MaxDepth` reference (a GraphicsDevice property, not a
        // static constant).
        Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
              color, getViewportProperty().getMaxDepthProperty(), 0);
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
        if (backend_ == nullptr)
        {
            return;
        }

        if (hasClearFlag(options, ClearOptions::DepthBuffer))
        {
            if (depth < 0.0f || depth > 1.0f)
                throw System::ArgumentOutOfRangeException(
                    "depth", std::to_string(depth),
                    "'depth' must be between 0.0 and 1.0.");
        }

        // Matches FNA's own GraphicsDevice.Clear(ClearOptions, ...), which masks DepthBuffer/
        // Stencil out of `options` when the currently active target has no real depth-stencil
        // buffer, rather than forwarding a clear request the backend cannot honor. Ask the
        // BACKEND (Task 708's own precedent for RenderTarget2D), not the merely-requested XNA-
        // level format, since a backend may honor no depth/stencil buffer at all regardless of
        // what was requested (SDL_Renderer is entirely 2D-only and never has one). Without this,
        // GraphicsDevice::Clear(const Color&) -- which unconditionally requests
        // Target|DepthBuffer|Stencil, matching FNA's own single-argument overload -- crashes on
        // SDL_RENDERER instead of degrading to a color-only clear.
        bool hasRealDepthBuffer;
        if (!currentRenderTargets_.empty())
        {
            const auto* rt = dynamic_cast<RenderTarget2D*>(currentRenderTargets_[0].getRenderTargetProperty());
            const bool depthFormatRequested = rt && rt->getDepthStencilFormatProperty() != DepthFormat::None;
            const auto* rtBackend = rt ? rt->GetRenderTargetBackend() : nullptr;
            hasRealDepthBuffer = rtBackend && rtBackend->HasRealDepthBuffer(depthFormatRequested);
        }
        else
        {
            hasRealDepthBuffer = backend_->SupportsDepthStencil();
        }
        if (!hasRealDepthBuffer)
        {
            options &= ClearOptions::Target;
        }

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const bool clearTarget  = hasClearFlag(options, ClearOptions::Target);
        const bool clearDepth   = hasClearFlag(options, ClearOptions::DepthBuffer);
        // Task 871: ClearOptions::Stencil was previously entirely ignored here -- neither checked
        // against `options` nor threaded through to any backend, so a requested stencil clear
        // silently did nothing on every backend.
        const bool clearStencil = hasClearFlag(options, ClearOptions::Stencil);

        if (clearTarget && clearDepth && clearStencil)
        {
            backend_->ClearColorDepthAndStencil(r, g, b, a, depth, stencil);
        }
        else if (clearTarget && clearDepth)
        {
            backend_->ClearColorAndDepth(r, g, b, a, depth);
        }
        else if (clearTarget && clearStencil)
        {
            backend_->ClearColorAndStencil(r, g, b, a, stencil);
        }
        else if (clearDepth && clearStencil)
        {
            backend_->ClearDepthAndStencil(depth, stencil);
        }
        else if (clearTarget)
        {
            backend_->Clear(r, g, b, a);
        }
        else if (clearDepth)
        {
            backend_->ClearDepth(depth);
        }
        else if (clearStencil)
        {
            backend_->ClearStencil(stencil);
        }
    }

    void GraphicsDevice::Clear(const Color& color, float depth)
    {
        Clear(ClearOptions::Target | ClearOptions::DepthBuffer, color, depth, 0);
    }

    void GraphicsDevice::Present()
    {
        if (renderTargetBound_)
            throw System::InvalidOperationException("Cannot present while render targets are bound");

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

        // The Touch Panel needs this too, for the same reason as the constructor.
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayWidthProperty(virtualWidth_);
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::setDisplayHeightProperty(virtualHeight_);

        applyPresentationParametersToWindow();

        if (backend_ != nullptr)
        {
            backend_->SetVirtualResolution(virtualWidth_, virtualHeight_);

            // Task 902: reconfigure the backend's actual MSAA sample count in place, mirroring
            // FNA's own PresentationParameters.MultiSampleCount = FNA3D_GetMaxMultiSampleCount(...)
            // write-back of the real, device-clamped value after FNA3D_ResetBackbuffer().
            const int appliedMultiSampleCount = backend_->ApplyMultiSampleCount(
                presentationParameters_.getMultiSampleCountProperty());
            presentationParameters_.setMultiSampleCountProperty(appliedMultiSampleCount);

            // plan_dx9.md D9-30/D9-33: same "actually reach the backend" rationale as
            // ApplyMultiSampleCount above, for back-buffer/depth-stencil format and fullscreen --
            // needed because Game commonly constructs this GraphicsDevice (and its backend) with
            // default PresentationParameters before GraphicsDeviceManager.ApplyChanges() ever runs.
            backend_->UpdatePresentationFormatEXT(
                static_cast<int>(presentationParameters_.getBackBufferFormatProperty()),
                static_cast<int>(presentationParameters_.getDepthStencilFormatProperty()),
                presentationParameters_.getIsFullScreenProperty());
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

        // Copy and clear the resource list before iterating.
        // This makes RemoveResourceReference a no-op when called re-entrantly
        // from within the resources' own Dispose() methods (matches FNA pattern).
        std::vector<GraphicsResource*> toDispose = std::move(resources_);
        resources_.clear();

        for (GraphicsResource* res : toDispose)
            static_cast<System::IDisposable*>(res)->Dispose();

        Disposing.Raise(this, System::EventArgs::Empty);
        destroyNativeResources();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        isDisposed_ = true;
    }

    void GraphicsDevice::OnResourceCreated(System::Object* resource)
    {
        if (!ResourceCreated.Empty())
            ResourceCreated.Raise(this, ResourceCreatedEventArgs(resource));
    }

    void GraphicsDevice::OnResourceDestroyed(const std::string& name, System::Object* tag)
    {
        if (!ResourceDestroyed.Empty())
            ResourceDestroyed.Raise(this, ResourceDestroyedEventArgs(name, tag));
    }

    void GraphicsDevice::AddResourceReference(GraphicsResource* resource)
    {
        resources_.push_back(resource);
    }

    void GraphicsDevice::RemoveResourceReference(GraphicsResource* resource)
    {
        for (std::size_t i = 0; i < resources_.size(); ++i)
        {
            if (resources_[i] == resource)
            {
                // Unordered removal — list order does not matter
                resources_[i] = resources_.back();
                resources_.pop_back();
                return;
            }
        }
    }

    void GraphicsDevice::SetDepthTestEnabled(bool enabled)
    {
        if (backend_ != nullptr) backend_->SetDepthTestEnabled(enabled);
    }

    void GraphicsDevice::SetBlendEnabled(bool enabled)
    {
        if (backend_ != nullptr) backend_->SetBlendEnabled(enabled);
    }

    void GraphicsDevice::SetDepthWriteEnabled(bool enabled)
    {
        if (backend_ != nullptr) backend_->SetDepthWriteEnabled(enabled);
    }

    void GraphicsDevice::SetGraphicsProfileEXT(GraphicsProfile profile)
    {
        graphicsProfile_ = profile;
    }

    void GraphicsDevice::SetVertexBuffer(const VertexBuffer* vertexBuffer)
    {
        if (vertexBuffer && vertexBuffer->getIsDisposedProperty())
            throw System::ObjectDisposedException(vertexBuffer->getNameProperty());
        currentVertexBuffer_ = vertexBuffer;
    }

    void GraphicsDevice::SetIndexBuffer(const IndexBuffer* indexBuffer)
    {
        if (indexBuffer && indexBuffer->getIsDisposedProperty())
            throw System::ObjectDisposedException(indexBuffer->getNameProperty());
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

    namespace
    {
        void ExtractMatrices(const Effect* effect, Matrix& world, Matrix& view, Matrix& proj)
        {
            world = Matrix::getIdentityProperty();
            view  = Matrix::getIdentityProperty();
            proj  = Matrix::getIdentityProperty();
            if (const auto* m = dynamic_cast<const IEffectMatrices*>(effect))
            {
                world = m->getWorldProperty();
                view  = m->getViewProperty();
                proj  = m->getProjectionProperty();
            }
        }
    }

    void GraphicsDevice::DrawPrimitives(PrimitiveType primitiveType, int vertexStart, int primitiveCount)
    {
        if (backend_ == nullptr)
            return;

        if (currentVertexBuffer_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: no vertex buffer is bound.");

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        System::ArgumentOutOfRangeException::ThrowIfNegative(vertexStart, "vertexStart");

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Backends::GpuDrawParams p;
        currentEffect_->FillGpuDrawParams(p);
        p.vertexStart = vertexStart;
        applySamplerStatesToBackend();
        backend_->DrawPrimitivesEx(
            currentVertexBuffer_->GetBackend(),
            world, view, proj,
            primitiveType, primitiveCount, p
        );
    }

    void GraphicsDevice::DrawIndexedPrimitives(
        PrimitiveType primitiveType,
        int baseVertex,
        int minVertexIndex,
        int numVertices,
        int startIndex,
        int primitiveCount
    )
    {
        (void)minVertexIndex;
        (void)numVertices;

        if (backend_ == nullptr)
            return;

        if (currentVertexBuffer_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound.");

        if (currentIndexBuffer_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no index buffer is bound.");

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawIndexedPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        System::ArgumentOutOfRangeException::ThrowIfNegative(startIndex, "startIndex");
        System::ArgumentOutOfRangeException::ThrowIfNegative(baseVertex, "baseVertex");

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Backends::GpuDrawParams p;
        currentEffect_->FillGpuDrawParams(p);
        p.startIndex = startIndex;
        p.baseVertex = baseVertex;
        applySamplerStatesToBackend();
        backend_->DrawIndexedPrimitivesEx(
            currentVertexBuffer_->GetBackend(),
            currentIndexBuffer_->GetBackend(),
            world, view, proj,
            primitiveType, primitiveCount, p
        );
    }

    void GraphicsDevice::DrawInstancedPrimitives(
        PrimitiveType primitiveType,
        int baseVertex,
        int minVertexIndex,
        int numVertices,
        int startIndex,
        int primitiveCount,
        int instanceCount
    )
    {
        (void)minVertexIndex;
        (void)numVertices;

        if (backend_ == nullptr)
            return;

        if (currentVertexBuffer_ == nullptr)
            throw std::runtime_error(
                "GraphicsDevice::DrawInstancedPrimitives: no vertex buffer is bound.");

        if (currentIndexBuffer_ == nullptr)
            throw std::runtime_error(
                "GraphicsDevice::DrawInstancedPrimitives: no index buffer is bound.");

        if (currentEffect_ == nullptr)
            throw std::runtime_error(
                "GraphicsDevice::DrawInstancedPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        System::ArgumentOutOfRangeException::ThrowIfNegative(startIndex, "startIndex");
        System::ArgumentOutOfRangeException::ThrowIfNegative(baseVertex, "baseVertex");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(instanceCount, "instanceCount");

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Backends::GpuDrawParams p;
        currentEffect_->FillGpuDrawParams(p);
        p.instanceCount = instanceCount;
        p.startIndex    = startIndex;
        p.baseVertex    = baseVertex;
        // Find the per-instance vertex buffer binding (instanceFrequency > 0).
        for (const auto& binding : currentVertexBuffers_) {
            if (binding.getInstanceFrequencyProperty() > 0) {
                if (auto* vb = binding.getVertexBufferProperty()) {
                    p.instanceVb = &vb->GetBackend();
                    break;
                }
            }
        }
        applySamplerStatesToBackend();
        backend_->DrawInstancedPrimitivesEx(
            currentVertexBuffer_->GetBackend(),
            currentIndexBuffer_->GetBackend(),
            world, view, proj,
            primitiveType, primitiveCount, instanceCount, p
        );
    }

    void GraphicsDevice::DrawUserPrimitives(
        PrimitiveType primitiveType,
        const void* vertexData,
        int vertexOffset,
        int primitiveCount
    )
    {
        if (backend_ == nullptr)
            return;

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");

        // Compute vertex count from primitive type (mirrors FNA PrimitiveVerts).
        int totalVerts;
        switch (primitiveType)
        {
            case PrimitiveType::TriangleList:  totalVerts = primitiveCount * 3; break;
            case PrimitiveType::TriangleStrip: totalVerts = primitiveCount + 2; break;
            case PrimitiveType::LineList:      totalVerts = primitiveCount * 2; break;
            case PrimitiveType::LineStrip:     totalVerts = primitiveCount + 1; break;
            case PrimitiveType::PointListEXT:  totalVerts = primitiveCount;     break;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }

        // vertexData points to an array of VertexPositionColor starting at vertexOffset.
        const auto* vertices = static_cast<const VertexPositionColor*>(vertexData) + vertexOffset;

        // Pack into the compact GPU layout that the backend expects (16 bytes: vec3 + 4 ubytes).
        struct GpuVertex { float x, y, z; std::uint8_t r, g, b, a; };
        static_assert(sizeof(GpuVertex) == 16, "GpuVertex must be 16 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(totalVerts));
        for (int i = 0; i < totalVerts; ++i)
        {
            packed[i].x = vertices[i].Position.X;
            packed[i].y = vertices[i].Position.Y;
            packed[i].z = vertices[i].Position.Z;
            packed[i].r = static_cast<std::uint8_t>(vertices[i].Color.getRProperty());
            packed[i].g = static_cast<std::uint8_t>(vertices[i].Color.getGProperty());
            packed[i].b = static_cast<std::uint8_t>(vertices[i].Color.getBProperty());
            packed[i].a = static_cast<std::uint8_t>(vertices[i].Color.getAProperty());
        }

        // Upload to a temporary vertex buffer and draw.
        auto tmpVb = backend_->CreateVertexBuffer(totalVerts);
        tmpVb->SetData(packed.data(), totalVerts, sizeof(GpuVertex));

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        applySamplerStatesToBackend();
        backend_->DrawColoredPrimitives(*tmpVb, world, view, proj, primitiveType, primitiveCount);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(
        PrimitiveType primitiveType,
        const void* vertexData,
        int vertexOffset,
        int numVertices,
        const void* indexData,
        int indexOffset,
        int primitiveCount
    )
    {
        if (backend_ == nullptr)
            return;

        if (currentEffect_ == nullptr)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");

        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");

        // Compute total index count from primitive type (mirrors FNA PrimitiveVerts).
        int indexCount = 0;
        switch (primitiveType)
        {
            case PrimitiveType::TriangleList:  indexCount = primitiveCount * 3; break;
            case PrimitiveType::TriangleStrip: indexCount = primitiveCount + 2; break;
            case PrimitiveType::LineList:      indexCount = primitiveCount * 2; break;
            case PrimitiveType::LineStrip:     indexCount = primitiveCount + 1; break;
            case PrimitiveType::PointListEXT:  indexCount = primitiveCount;     break;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }

        // Pack vertices from caller array (assumed VertexPositionColor layout).
        const auto* vertices = static_cast<const VertexPositionColor*>(vertexData) + vertexOffset;
        struct GpuVertex { float x, y, z; std::uint8_t r, g, b, a; };
        static_assert(sizeof(GpuVertex) == 16, "GpuVertex must be 16 bytes");

        std::vector<GpuVertex> packed(static_cast<std::size_t>(numVertices));
        for (int i = 0; i < numVertices; ++i)
        {
            packed[i].x = vertices[i].Position.X;
            packed[i].y = vertices[i].Position.Y;
            packed[i].z = vertices[i].Position.Z;
            packed[i].r = static_cast<std::uint8_t>(vertices[i].Color.getRProperty());
            packed[i].g = static_cast<std::uint8_t>(vertices[i].Color.getGProperty());
            packed[i].b = static_cast<std::uint8_t>(vertices[i].Color.getBProperty());
            packed[i].a = static_cast<std::uint8_t>(vertices[i].Color.getAProperty());
        }

        // Copy 16-bit indices with offset applied.
        const auto* indices = static_cast<const std::uint16_t*>(indexData) + indexOffset;
        std::vector<std::uint16_t> indexCopy(static_cast<std::size_t>(indexCount));
        for (int i = 0; i < indexCount; ++i)
            indexCopy[i] = indices[i];

        auto tmpVb = backend_->CreateVertexBuffer(numVertices);
        tmpVb->SetData(packed.data(), numVertices, sizeof(GpuVertex));

        auto tmpIb = backend_->CreateIndexBuffer16(indexCount);
        tmpIb->SetData16(indexCopy.data(), indexCount);

        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        applySamplerStatesToBackend();
        backend_->DrawIndexedColoredPrimitives(*tmpVb, *tmpIb, world, view, proj, primitiveType, primitiveCount);
    }

    // -----------------------------------------------------------------------
    // DrawUserPrimitives — typed overloads
    // -----------------------------------------------------------------------

    // Public NOXNA static — mirrors FNA's private PrimitiveVerts().
    int GraphicsDevice::PrimitiveVerts(PrimitiveType type, int primitiveCount)
    {
        switch (type)
        {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
        }
    }

    namespace
    {
        int VertexCountForUserPrimitives(PrimitiveType type, int primitiveCount)
        {
            return GraphicsDevice::PrimitiveVerts(type, primitiveCount);
        }

        // GPU-layout packed structs (no vtable, exact stride).
        struct GpuVPC  { float x,y,z; std::uint8_t r,g,b,a; };                        // 16
        struct GpuVPT  { float x,y,z,u,v; };                                           // 20
        struct GpuVPCT { float x,y,z; std::uint8_t r,g,b,a; float u,v; };             // 24
        struct GpuVPNT { float x,y,z, nx,ny,nz, u,v; };                               // 32

        static_assert(sizeof(GpuVPC)  == 16);
        static_assert(sizeof(GpuVPT)  == 20);
        static_assert(sizeof(GpuVPCT) == 24);
        static_assert(sizeof(GpuVPNT) == 32);
    }

    // Grows the scratch buffer only when the requested size exceeds current capacity, so
    // steady-state DrawUserPrimitives/DrawUserIndexedPrimitives calls (same or shrinking vertex
    // counts) reuse the existing allocation instead of allocating on every draw.
    void* GraphicsDevice::AcquireUserVertexScratch(std::size_t bytes)
    {
        if (userVertexScratch_.size() < bytes) userVertexScratch_.resize(bytes);
        return userVertexScratch_.data();
    }

    void* GraphicsDevice::AcquireUserIndexScratch(std::size_t bytes)
    {
        if (userIndexScratch_.size() < bytes) userIndexScratch_.resize(bytes);
        return userIndexScratch_.data();
    }

    // DrawUserPrimitives — VertexPositionColor
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColor* data, int offset, int count)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPC*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPC)));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPC));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionTexture* data, int offset, int count)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPT*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPT)));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionColorTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColorTexture* data, int offset, int count)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPCT*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPCT)));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPCT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionNormalTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionNormalTexture* data, int offset, int count)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        auto* packed = static_cast<GpuVPNT*>(AcquireUserVertexScratch(static_cast<std::size_t>(n) * sizeof(GpuVPNT)));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed, n, sizeof(GpuVPNT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — explicit VertexDeclaration (FNA second generic overload)
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const void* vertexData, int vertexOffset,
                                            int primitiveCount,
                                            const VertexDeclaration& vertexDeclaration)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primitiveCount, "primitiveCount");
        const int n      = VertexCountForUserPrimitives(type, primitiveCount);
        const int stride = vertexDeclaration.getVertexStrideProperty();
        // Apply vertexOffset in bytes then upload n vertices worth of raw data.
        const auto* src = static_cast<const std::uint8_t*>(vertexData)
                          + static_cast<std::ptrdiff_t>(vertexOffset) * stride;
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(src, n, static_cast<std::size_t>(stride));
        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
        applySamplerStatesToBackend();
        backend_->DrawPrimitivesEx(*vb, world, view, proj, type, primitiveCount, p);
    }

    // -----------------------------------------------------------------------
    // DrawUserIndexedPrimitives — typed overloads
    // -----------------------------------------------------------------------

    namespace
    {
        int IndexCountForPrimitives(PrimitiveType type, int primitiveCount)
        {
            switch (type)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
                default:
                    throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColor* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPC*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPC)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPC));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPT)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPT));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColorTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPCT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPCT)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPCT));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionNormalTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPNT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPNT)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPNT));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    // DrawUserIndexedPrimitives — 32-bit index overloads

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColor* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPC*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPC)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPC));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPT)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPT));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColorTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPCT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPCT)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPCT));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionNormalTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic = IndexCountForPrimitives(type, primCount);
        auto* packed = static_cast<GpuVPNT*>(AcquireUserVertexScratch(static_cast<std::size_t>(numVerts) * sizeof(GpuVPNT)));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indices + iOffset, indices + iOffset + ic, idx);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed, numVerts, sizeof(GpuVPNT));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          applySamplerStatesToBackend();
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    // DrawUserIndexedPrimitives — explicit VertexDeclaration (FNA second generic overloads)

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const void* vertexData, int vOffset, int numVerts,
                                                   const std::uint16_t* indexData, int iOffset, int primCount,
                                                   const VertexDeclaration& vd)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic     = IndexCountForPrimitives(type, primCount);
        const int stride = vd.getVertexStrideProperty();
        const auto* src  = static_cast<const std::uint8_t*>(vertexData)
                           + static_cast<std::ptrdiff_t>(vOffset) * stride;
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(src, numVerts, static_cast<std::size_t>(stride));
        auto* idx = static_cast<std::uint16_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint16_t)));
        std::copy(indexData + iOffset, indexData + iOffset + ic, idx);
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx, ic);
        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
        applySamplerStatesToBackend();
        backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p);
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const void* vertexData, int vOffset, int numVerts,
                                                   const std::uint32_t* indexData, int iOffset, int primCount,
                                                   const VertexDeclaration& vd)
    {
        if (!backend_) return;
        if (!currentEffect_)
            throw std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(primCount, "primitiveCount");
        const int ic     = IndexCountForPrimitives(type, primCount);
        const int stride = vd.getVertexStrideProperty();
        const auto* src  = static_cast<const std::uint8_t*>(vertexData)
                           + static_cast<std::ptrdiff_t>(vOffset) * stride;
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(src, numVerts, static_cast<std::size_t>(stride));
        auto* idx = static_cast<std::uint32_t*>(AcquireUserIndexScratch(static_cast<std::size_t>(ic) * sizeof(std::uint32_t)));
        std::copy(indexData + iOffset, indexData + iOffset + ic, idx);
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx, ic);
        Matrix world, view, proj;
        ExtractMatrices(currentEffect_, world, view, proj);
        CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
        applySamplerStatesToBackend();
        backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p);
    }

    CNA::Internal::Backends::IGraphicsBackend& GraphicsDevice::GetBackend() const
    {
        if (backend_ == nullptr)
        {
            throw std::runtime_error("GraphicsDevice backend is not available.");
        }

        return *backend_;
    }

    void GraphicsDevice::SetCurrentEffect(Effect* effect)
    {
        currentEffect_ = effect;
    }

    const std::string& GraphicsDevice::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.GraphicsDevice";
        return typeName;
    }

    void GraphicsDevice::SetPresentationParameters(const PresentationParameters& pp)
    {
        presentationParameters_ = pp;
        if (backend_)
            backend_->SetSwapInterval(toSwapInterval(pp.getPresentationIntervalProperty()));
    }

    void GraphicsDevice::RecreateBackendForMultiSampleCount(int multiSampleCount)
    {
        presentationParameters_.setMultiSampleCountProperty(multiSampleCount);
        backend_.reset();
        createBackend();
        UpdateViewportFromWindow();
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
#if defined(CNA_BACKEND_HEADLESS) || defined(CNA_BACKEND_SOFTWARE)
        // No real window, ever -- see the constructor's matching guard above.
        // GraphicsBackendCreateArgs::window stays nullptr; UpdateViewportFromWindow() already
        // falls back to the backend's own GetViewportSize() first and only touches window_ if
        // that yields nothing, and applyPresentationParametersToWindow() already early-returns
        // when window_ is null, so neither needs its own guard.
        window_ = nullptr;
        ownsWindow_ = false;
#else
        // Runtime opt-in, same effect as the compile-time branch above. Only backends that can
        // genuinely run without a swap chain support this (D3D12 today) -- see
        // PresentationParameters::getHeadlessEXTProperty()'s own doc comment. A backend that cannot
        // (D3D11's constructor always creates a swap chain; EasyGL's GL context is bound to a
        // window) will throw from its own constructor, which is the honest outcome: it is a real
        // "this backend cannot do that" error, not something GraphicsDevice should paper over.
        if (presentationParameters_.getHeadlessEXTProperty())
        {
            window_ = nullptr;
            ownsWindow_ = false;
            return;
        }

        const auto requestedHandle = presentationParameters_.getDeviceWindowHandleProperty();
        if (requestedHandle != 0)
        {
            window_ = reinterpret_cast<SDL_Window*>(requestedHandle);
            ownsWindow_ = false;
            return;
        }

        SDL_WindowFlags windowFlags = getBackendWindowFlags();

        const int width = presentationParameters_.getBackBufferWidthProperty() > 0
                              ? presentationParameters_.getBackBufferWidthProperty()
                              : 1024;

        const int height = presentationParameters_.getBackBufferHeightProperty() > 0
                               ? presentationParameters_.getBackBufferHeightProperty()
                               : 768;

        window_ = SDL_CreateWindow("Game", width, height, windowFlags);
        if (window_ == nullptr)
        {
            throw makeSdlError("SDL_CreateWindow");
        }

        ownsWindow_ = true;
        presentationParameters_.
            setDeviceWindowHandleProperty(reinterpret_cast<PresentationParameters::IntPtr>(window_));

        // Publish the window to the text-input subsystem (mirrors FNA, which sets
        // TextInputEXT.WindowHandle at window creation). Required for StartTextInput etc.
        Microsoft::Xna::Framework::Input::TextInputEXT::setWindowHandleProperty(
            reinterpret_cast<std::uintptr_t>(window_));

        // Publish the same window to Mouse (mirrors FNA setting Mouse.WindowHandle at window
        // creation, SDL3_FNAPlatform.cs). Lets SetPosition / relative-mouse-mode target the
        // real window instead of relying on the SDL_GetMouseFocus() fallback.
        Microsoft::Xna::Framework::Input::Mouse::setWindowHandleProperty(
            reinterpret_cast<std::uintptr_t>(window_));

        LogWindowDebugState(window_, "after SDL_CreateWindow");
#endif
    }

    void GraphicsDevice::SetContextRecoveryEnabled(bool enabled)
    {
        contextRecoveryEnabled_ = enabled;
        if (backend_)
            backend_->SetContextRecoveryEnabled(enabled);
    }

    void GraphicsDevice::SetStringMarkerEXT(const std::string& marker)
    {
        if (backend_)
            backend_->SetStringMarkerEXT(marker.c_str());
    }

    void GraphicsDevice::createBackend()
    {
        GraphicsBackendCreateArgs args;
        args.window = window_;
        args.virtualWidth = virtualWidth_;
        args.virtualHeight = virtualHeight_;
        args.contextRecoveryEnabled = contextRecoveryEnabled_;
        args.multiSampleCount = presentationParameters_.getMultiSampleCountProperty();
        args.swapInterval = toSwapInterval(presentationParameters_.getPresentationIntervalProperty());
        // plan_dx9.md D9-30: real presentation-parameter fidelity for backends that need it (D3D9);
        // every other backend continues to ignore these exactly as before the fields existed.
        args.backBufferFormat = static_cast<int>(presentationParameters_.getBackBufferFormatProperty());
        args.depthStencilFormat = static_cast<int>(presentationParameters_.getDepthStencilFormatProperty());
        args.isFullScreen = presentationParameters_.getIsFullScreenProperty();
        args.graphicsProfile = static_cast<int>(graphicsProfile_);
        // plan_dx9.md D9-34: forward a REAL, backend-detected device-lost/reset event to this
        // GraphicsDevice's own public XNA events. Nine of the ten backends never call this.
        args.deviceEventCallback = [this](CNA::Internal::Backends::BackendDeviceEvent event)
        {
            switch (event)
            {
                case CNA::Internal::Backends::BackendDeviceEvent::Lost:
                    deviceStatus_ = GraphicsDeviceStatus::Lost;
                    DeviceLost.Raise(this, System::EventArgs::Empty);
                    break;
                case CNA::Internal::Backends::BackendDeviceEvent::Resetting:
                    deviceStatus_ = GraphicsDeviceStatus::NotReset;
                    DeviceResetting.Raise(this, System::EventArgs::Empty);
                    break;
                case CNA::Internal::Backends::BackendDeviceEvent::Reset:
                    deviceStatus_ = GraphicsDeviceStatus::Normal;
                    DeviceReset.Raise(this, System::EventArgs::Empty);
                    break;
            }
        };

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
            // Clear the text-input window handle if it points at this window
            // (mirrors FNA DisposeWindow, SDL3_FNAPlatform.cs:463-466).
            if (Microsoft::Xna::Framework::Input::TextInputEXT::getWindowHandleProperty()
                == reinterpret_cast<std::uintptr_t>(window_))
            {
                Microsoft::Xna::Framework::Input::TextInputEXT::setWindowHandleProperty(0);
            }
            if (Microsoft::Xna::Framework::Input::Mouse::getWindowHandleProperty()
                == reinterpret_cast<std::uintptr_t>(window_))
            {
                Microsoft::Xna::Framework::Input::Mouse::setWindowHandleProperty(0);
            }
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

        // Compared against the last size *this method itself* produced, not against
        // viewport_'s current width/height: viewport_ may hold a game-set custom
        // sub-region Viewport (e.g. split-screen) whose dimensions legitimately differ
        // from the backbuffer, and FNA's Present() never touches Viewport at all. Using
        // viewport_ as the "did anything change" signal would silently stomp such a
        // Viewport back to full-window size on the very next Present() call even though
        // no resize occurred.
        if (width == lastKnownViewportWidth_ && height == lastKnownViewportHeight_)
        {
            return;
        }

        lastKnownViewportWidth_ = width;
        lastKnownViewportHeight_ = height;

        viewport_.setXProperty(0);
        viewport_.setYProperty(0);
        viewport_.setMinDepthProperty(0.0f);
        viewport_.setMaxDepthProperty(1.0f);
        viewport_.setWidthProperty(width);
        viewport_.setHeightProperty(height);

        // Mutates viewport_'s fields directly (not via setViewportProperty(), to preserve the
        // "compared against lastKnownViewportWidth/Height_, not viewport_" semantics above) --
        // push the reset value to the backend explicitly (Task 880) so a window resize actually
        // updates the GPU-side viewport too, not just the C++-side Viewport property.
        if (backend_)
            backend_->SetViewport(0, 0, width, height, 0.0f, 1.0f);
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

    void GraphicsDevice::applySamplerStatesToBackend()
    {
        if (!backend_) return;
        for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
        {
            const SamplerState& ss = samplerStates_[i];
            backend_->ApplySamplerState(i,
                (int)ss.getFilterProperty(),
                (int)ss.getAddressUProperty(),
                (int)ss.getAddressVProperty(),
                ss.getMaxAnisotropyProperty());
        }
    }

    void GraphicsDevice::applyPresentationParametersToWindow()
    {
        if (window_ == nullptr)
        {
            return;
        }

        // Task 902: fullscreen switching may not be available in headless / virtual-display
        // test environments (Xvfb). The PP value is already stored above this call, so a
        // backend that cannot actually switch fullscreen still has the correct stored state --
        // matches GraphicsDeviceManager::applyToExistingBackend()'s identical non-fatal handling
        // (Task 224), which this method now supersedes as the single fullscreen-application path.
        const bool fullScreen = presentationParameters_.getIsFullScreenProperty();
        if (!SDL_SetWindowFullscreen(window_, fullScreen))
        {
            SDL_ClearError();
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

    // --- New XNA 4.0 API methods ---

    GraphicsDeviceStatus GraphicsDevice::getGraphicsDeviceStatusProperty() const
    {
        // plan_dx9.md D9-34: tracks the real backend-reported status via deviceStatus_ (updated by
        // the deviceEventCallback lambda in createBackend()). Every backend except D3D9 never calls
        // that callback, so this stays Normal for them -- identical behavior to before this field
        // existed.
        return deviceStatus_;
    }

    DisplayMode GraphicsDevice::getDisplayModeProperty() const
    {
        if (presentationParameters_.getIsFullScreenProperty())
        {
            int w = 0, h = 0;
            if (backend_) backend_->GetViewportSize(w, h);
            return DisplayMode(w, h, SurfaceFormat::Color);
        }
        return getAdapterProperty().getCurrentDisplayModeProperty();
    }

    TextureCollection& GraphicsDevice::getTexturesProperty() { return textures_; }
    SamplerStateCollection& GraphicsDevice::getSamplerStatesProperty() { return samplerStates_; }
    TextureCollection& GraphicsDevice::getVertexTexturesProperty() { return vertexTextures_; }
    SamplerStateCollection& GraphicsDevice::getVertexSamplerStatesProperty() { return vertexSamplerStates_; }

    BlendState& GraphicsDevice::getBlendStateProperty() { return blendState_; }
    const BlendState& GraphicsDevice::getBlendStateProperty() const { return blendState_; }
    void GraphicsDevice::setBlendStateProperty(const BlendState& value)
    {
        blendState_ = value;
        if (backend_)
            backend_->ApplyBlendState(
                (int)value.getColorSourceBlendProperty(),
                (int)value.getAlphaSourceBlendProperty(),
                (int)value.getColorDestinationBlendProperty(),
                (int)value.getAlphaDestinationBlendProperty(),
                (int)value.getColorBlendFunctionProperty(),
                (int)value.getAlphaBlendFunctionProperty());
        // FNA applies BlendState.BlendFactor atomically as part of FNA3D_SetBlendState — the
        // state's own baked-in blend factor becomes the device's current one, the same way
        // GraphicsDevice.BlendFactor's own setter would.
        setBlendFactorProperty(value.getBlendFactorProperty());
    }

    DepthStencilState& GraphicsDevice::getDepthStencilStateProperty() { return depthStencilState_; }
    const DepthStencilState& GraphicsDevice::getDepthStencilStateProperty() const { return depthStencilState_; }
    void GraphicsDevice::setDepthStencilStateProperty(const DepthStencilState& value)
    {
        depthStencilState_ = value;
        if (backend_)
            backend_->ApplyDepthStencilState(
                value.getDepthBufferEnableProperty(),
                value.getDepthBufferWriteEnableProperty(),
                (int)value.getDepthBufferFunctionProperty(),
                value.getStencilEnableProperty(),
                (int)value.getStencilFunctionProperty(),
                (int)value.getStencilPassProperty(),
                (int)value.getStencilFailProperty(),
                (int)value.getStencilDepthBufferFailProperty(),
                value.getStencilMaskProperty(),
                value.getStencilWriteMaskProperty(),
                value.getReferenceStencilProperty(),
                value.getTwoSidedStencilModeProperty(),
                (int)value.getCounterClockwiseStencilFunctionProperty(),
                (int)value.getCounterClockwiseStencilPassProperty(),
                (int)value.getCounterClockwiseStencilFailProperty(),
                (int)value.getCounterClockwiseStencilDepthBufferFailProperty());
        // FNA applies a DepthStencilState's own ReferenceStencil atomically as part of the whole
        // native state struct, the same way BlendState's own BlendFactor is applied (Task 309) -
        // keep GraphicsDevice.ReferenceStencil in sync with whatever state was just assigned.
        setReferenceStencilProperty(value.getReferenceStencilProperty());
    }

    RasterizerState& GraphicsDevice::getRasterizerStateProperty() { return rasterizerState_; }
    const RasterizerState& GraphicsDevice::getRasterizerStateProperty() const { return rasterizerState_; }
    void GraphicsDevice::setRasterizerStateProperty(const RasterizerState& value)
    {
        rasterizerState_ = value;
        if (backend_)
            backend_->ApplyRasterizerState(
                (int)value.getCullModeProperty(),
                (int)value.getFillModeProperty(),
                value.getScissorTestEnableProperty(),
                value.getDepthBiasProperty(),
                value.getSlopeScaleDepthBiasProperty());
    }

    Rectangle GraphicsDevice::getScissorRectangleProperty() const { return scissorRectangle_; }
    void GraphicsDevice::setScissorRectangleProperty(const Rectangle& value)
    {
        scissorRectangle_ = value;
        if (backend_)
            backend_->SetScissorRect(value.X, value.Y, value.Width, value.Height);
    }

    Color GraphicsDevice::getBlendFactorProperty() const { return blendFactor_; }
    void GraphicsDevice::setBlendFactorProperty(const Color& value)
    {
        blendFactor_ = value;
        if (backend_)
            backend_->SetBlendFactor(
                value.getRProperty() / 255.0f,
                value.getGProperty() / 255.0f,
                value.getBProperty() / 255.0f,
                value.getAProperty() / 255.0f);
    }

    int GraphicsDevice::getMultiSampleMaskProperty() const { return multiSampleMask_; }
    void GraphicsDevice::setMultiSampleMaskProperty(int value) { multiSampleMask_ = value; }

    int GraphicsDevice::getReferenceStencilProperty() const { return referenceStencil_; }
    void GraphicsDevice::setReferenceStencilProperty(int value)
    {
        referenceStencil_ = value;
        if (backend_)
            backend_->SetReferenceStencil(value);
    }

    void GraphicsDevice::Reset()
    {
        Reset(presentationParameters_, adapter_);
    }

    void GraphicsDevice::Reset(const PresentationParameters& presentationParameters)
    {
        Reset(presentationParameters, adapter_);
    }

    void GraphicsDevice::GetBackBufferData(Color* data, int elementCount)
    {
        GetBackBufferData(nullptr, data, 0, elementCount);
    }

    void GraphicsDevice::GetBackBufferData(Color* data, int startIndex, int elementCount)
    {
        GetBackBufferData(nullptr, data, startIndex, elementCount);
    }

    void GraphicsDevice::GetBackBufferData(const Rectangle* rect, Color* data, int startIndex, int elementCount)
    {
        if (data == nullptr)
            throw std::invalid_argument("data");

        int x, y, w, h;
        if (rect)
        {
            x = rect->X;
            y = rect->Y;
            w = rect->Width;
            h = rect->Height;
        }
        else
        {
            x = 0;
            y = 0;
            backend_->GetViewportSize(w, h);
        }

        if (elementCount < w * h)
            throw std::runtime_error("GetBackBufferData: data array too small for requested region");
        Texture::ValidateGetDataFormat(presentationParameters_.getBackBufferFormatProperty(), 4);

        // Color inherits a vtable pointer, so its first byte is NOT the R component.
        // Use a plain byte buffer for ReadBackbuffer, then unpack each RGBA group
        // into a Color(r, g, b, a) to avoid writing into the vtable pointer.
        const int pixelCount = w * h;
        std::vector<uint8_t> buf(static_cast<std::size_t>(pixelCount) * 4);
        backend_->ReadBackbuffer(x, y, w, h, buf.data());
        for (int i = 0; i < pixelCount; ++i)
        {
            const uint8_t* p = buf.data() + i * 4;
            data[startIndex + i] = Color(p[0], p[1], p[2], p[3]);
        }
    }

    void GraphicsDevice::ResetViewportAndScissorForRenderTarget(int width, int height)
    {
        setViewportProperty(Viewport(0, 0, width, height));
        setScissorRectangleProperty(Rectangle(0, 0, width, height));
    }

    void GraphicsDevice::SetRenderTarget(RenderTarget2D* renderTarget)
    {
        if (renderTarget && renderTarget->getIsDisposedProperty())
            throw System::ObjectDisposedException(renderTarget->getNameProperty());
        if (backend_)
            backend_->SetRenderTarget2D(renderTarget ? renderTarget->GetRenderTargetBackend() : nullptr);

        currentRenderTargets_.clear();
        renderTargetBound_ = (renderTarget != nullptr);
        if (renderTarget != nullptr)
            currentRenderTargets_.push_back(RenderTargetBinding(
                static_cast<Texture*>(renderTarget)));

        // Matches FNA: Viewport/ScissorRectangle always reset to the new render target's size
        // (or the backbuffer's, when unbinding) — never left at whatever was set before.
        if (renderTarget != nullptr)
            ResetViewportAndScissorForRenderTarget(renderTarget->getWidthProperty(),
                                                    renderTarget->getHeightProperty());
        else
            ResetViewportAndScissorForRenderTarget(presentationParameters_.getBackBufferWidthProperty(),
                                                    presentationParameters_.getBackBufferHeightProperty());

        if (renderTarget &&
            renderTarget->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents)
        {
            // Only ask for a depth-buffer clear when the target actually has one. A requested
            // DepthFormat::None never has one; beyond that, ask the BACKEND (Task 708) rather
            // than trusting the merely-requested XNA-level format, since a backend may honor no
            // depth format at all regardless of what was requested (SDL_Renderer's 2D-only
            // render targets never allocate real depth-buffer storage).
            const bool depthFormatRequested =
                renderTarget->getDepthStencilFormatProperty() != DepthFormat::None;
            const auto* rtBackend = renderTarget->GetRenderTargetBackend();
            const bool hasDepthBuffer =
                rtBackend && rtBackend->HasRealDepthBuffer(depthFormatRequested);
            Clear(hasDepthBuffer ? (ClearOptions::Target | ClearOptions::DepthBuffer) : ClearOptions::Target,
                  Color(0, 0, 0, 255), 1.0f, 0);
        }
    }

    void GraphicsDevice::SetRenderTarget(RenderTargetCube* renderTarget, CubeMapFace cubeMapFace)
    {
        if (renderTarget && renderTarget->getIsDisposedProperty())
            throw System::ObjectDisposedException(renderTarget->getNameProperty());
        if (backend_)
            backend_->SetRenderTargetCubeFace(
                renderTarget ? renderTarget->GetRenderTargetCubeBackend() : nullptr,
                static_cast<int>(cubeMapFace));

        currentRenderTargets_.clear();
        renderTargetBound_ = (renderTarget != nullptr);

        if (renderTarget != nullptr)
            ResetViewportAndScissorForRenderTarget(renderTarget->getWidthProperty(),
                                                    renderTarget->getHeightProperty());
        else
            ResetViewportAndScissorForRenderTarget(presentationParameters_.getBackBufferWidthProperty(),
                                                    presentationParameters_.getBackBufferHeightProperty());
    }

    void GraphicsDevice::SetRenderTargets(const std::vector<RenderTargetBinding>& renderTargets)
    {
        if (renderTargets.size() > MAX_RENDERTARGET_BINDINGS)
            throw std::invalid_argument("SetRenderTargets: at most " +
                std::to_string(MAX_RENDERTARGET_BINDINGS) + " render targets may be bound at once.");

        // Task 717 finding: SetRenderTarget(RenderTarget2D*) (singular) already guards against a
        // disposed target -- this plural overload didn't, letting a disposed RenderTarget2D reach
        // GetRenderTargetBackend() below, which (before this same task's RenderTarget2D::Dispose
        // fix) returned a dangling pointer -- a use-after-free crash instead of a clean exception.
        for (const auto& binding : renderTargets)
        {
            auto* rt = dynamic_cast<RenderTarget2D*>(binding.getRenderTargetProperty());
            if (rt && rt->getIsDisposedProperty())
                throw System::ObjectDisposedException(rt->getNameProperty());
        }

        currentRenderTargets_ = renderTargets;
        renderTargetBound_ = !renderTargets.empty();
        if (renderTargets.empty())
        {
            // Matches FNA: reset to the backbuffer's size when unbinding.
            ResetViewportAndScissorForRenderTarget(presentationParameters_.getBackBufferWidthProperty(),
                                                    presentationParameters_.getBackBufferHeightProperty());
            if (!backend_) return;
            backend_->SetRenderTargets(nullptr, 0);
            return;
        }
        if (!backend_) return;
        std::vector<CNA::Internal::Backends::IRenderTargetBackend*> backends;
        backends.reserve(renderTargets.size());
        for (const auto& binding : renderTargets)
        {
            auto* rt = dynamic_cast<RenderTarget2D*>(binding.getRenderTargetProperty());
            backends.push_back(rt ? rt->GetRenderTargetBackend() : nullptr);
        }
        backend_->SetRenderTargets(backends.data(), static_cast<int>(backends.size()));

        auto* first = dynamic_cast<RenderTarget2D*>(renderTargets[0].getRenderTargetProperty());
        // Matches FNA: Viewport/ScissorRectangle reset to the FIRST bound target's size.
        if (first)
            ResetViewportAndScissorForRenderTarget(first->getWidthProperty(), first->getHeightProperty());
        if (first &&
            first->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents)
        {
            // See SetRenderTarget(RenderTarget2D*)'s identical guard for the rationale.
            const bool depthFormatRequested =
                first->getDepthStencilFormatProperty() != DepthFormat::None;
            const auto* rtBackend = first->GetRenderTargetBackend();
            const bool hasDepthBuffer =
                rtBackend && rtBackend->HasRealDepthBuffer(depthFormatRequested);
            Clear(hasDepthBuffer ? (ClearOptions::Target | ClearOptions::DepthBuffer) : ClearOptions::Target,
                  Color(0, 0, 0, 255), 1.0f, 0);
        }
    }

    std::vector<RenderTargetBinding> GraphicsDevice::GetRenderTargets() const
    {
        return currentRenderTargets_;
    }

    void GraphicsDevice::SetVertexBuffer(const VertexBuffer* vertexBuffer, int /*vertexOffset*/)
    {
        SetVertexBuffer(vertexBuffer);
    }

    void GraphicsDevice::SetVertexBuffers(const std::vector<VertexBufferBinding>& vertexBuffers)
    {
        constexpr int kMaxVertexBufferBindings = 16; // XNA4 HiDef spec limit
        if (static_cast<int>(vertexBuffers.size()) > kMaxVertexBufferBindings)
            throw System::ArgumentOutOfRangeException(
                "vertexBuffers",
                std::to_string(vertexBuffers.size()),
                "Max Vertex Buffers supported is " + std::to_string(kMaxVertexBufferBindings));

        currentVertexBuffers_ = vertexBuffers;
        if (!vertexBuffers.empty())
            currentVertexBuffer_ = vertexBuffers[0].getVertexBufferProperty();
    }

    std::vector<VertexBufferBinding> GraphicsDevice::GetVertexBuffers() const
    {
        return currentVertexBuffers_;
    }
}
