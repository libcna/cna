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

#ifdef CNA_BACKEND_BGFX
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif

#include <SDL3/SDL.h>

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
          blendFactor_(Color::White)
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
        (void)stencil;

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
            backend_->ClearDepth(depth);
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
        backend_->DrawIndexedColoredPrimitives(*tmpVb, *tmpIb, world, view, proj, primitiveType, primitiveCount);
    }

    // -----------------------------------------------------------------------
    // DrawUserPrimitives — typed overloads
    // -----------------------------------------------------------------------

    namespace
    {
        // Returns the vertex count implied by a primitive type and count.
        int VertexCountForUserPrimitives(PrimitiveType type, int primitiveCount)
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

    // DrawUserPrimitives — VertexPositionColor
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColor* data, int offset, int count)
    {
        if (!backend_ || !currentEffect_) return;
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        std::vector<GpuVPC> packed(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed.data(), n, sizeof(GpuVPC));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionTexture* data, int offset, int count)
    {
        if (!backend_ || !currentEffect_) return;
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        std::vector<GpuVPT> packed(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed.data(), n, sizeof(GpuVPT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionColorTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionColorTexture* data, int offset, int count)
    {
        if (!backend_ || !currentEffect_) return;
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        std::vector<GpuVPCT> packed(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed.data(), n, sizeof(GpuVPCT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
    }

    // DrawUserPrimitives — VertexPositionNormalTexture
    void GraphicsDevice::DrawUserPrimitives(PrimitiveType type,
                                            const VertexPositionNormalTexture* data, int offset, int count)
    {
        if (!backend_ || !currentEffect_) return;
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(count, "primitiveCount");
        const int n = VertexCountForUserPrimitives(type, count);
        std::vector<GpuVPNT> packed(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const auto& v = data[offset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        auto vb = backend_->CreateVertexBuffer(n);
        vb->SetData(packed.data(), n, sizeof(GpuVPNT));
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, p); }
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
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPC> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        std::vector<std::uint16_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPC));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPT> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        std::vector<std::uint16_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPT));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColorTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPCT> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        std::vector<std::uint16_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPCT));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionNormalTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint16_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPNT> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        std::vector<std::uint16_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPNT));
        auto ib = backend_->CreateIndexBuffer16(ic);
        ib->SetData16(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    // DrawUserIndexedPrimitives — 32-bit index overloads

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColor* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPC> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty() };
        }
        std::vector<std::uint32_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPC));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPT> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        std::vector<std::uint32_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPT));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionColorTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPCT> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Color.getRProperty(), v.Color.getGProperty(),
                          v.Color.getBProperty(), v.Color.getAProperty(),
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        std::vector<std::uint32_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPCT));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
    }

    void GraphicsDevice::DrawUserIndexedPrimitives(PrimitiveType type,
                                                   const VertexPositionNormalTexture* vertices, int vOffset, int numVerts,
                                                   const std::uint32_t* indices, int iOffset, int primCount)
    {
        if (!backend_ || !currentEffect_) return;
        const int ic = IndexCountForPrimitives(type, primCount);
        std::vector<GpuVPNT> packed(static_cast<std::size_t>(numVerts));
        for (int i = 0; i < numVerts; ++i)
        {
            const auto& v = vertices[vOffset + i];
            packed[i] = { v.Position.X, v.Position.Y, v.Position.Z,
                          v.Normal.X, v.Normal.Y, v.Normal.Z,
                          v.TextureCoordinate.X, v.TextureCoordinate.Y };
        }
        std::vector<std::uint32_t> idx(indices + iOffset, indices + iOffset + ic);
        auto vb = backend_->CreateVertexBuffer(numVerts);
        vb->SetData(packed.data(), numVerts, sizeof(GpuVPNT));
        auto ib = backend_->CreateIndexBuffer32(ic);
        ib->SetData32(idx.data(), ic);
        { Matrix world, view, proj;
          ExtractMatrices(currentEffect_, world, view, proj);
          CNA::Internal::Backends::GpuDrawParams p; currentEffect_->FillGpuDrawParams(p);
          backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, p); }
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

        LogWindowDebugState(window_, "after SDL_CreateWindow");
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

    // --- New XNA 4.0 API methods ---

    GraphicsDeviceStatus GraphicsDevice::getGraphicsDeviceStatusProperty() const
    {
        return GraphicsDeviceStatus::Normal;
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
                value.getScissorTestEnableProperty());
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
    void GraphicsDevice::setReferenceStencilProperty(int value) { referenceStencil_ = value; }

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

        if (renderTarget &&
            renderTarget->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents)
        {
            Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
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
    }

    void GraphicsDevice::SetRenderTargets(const std::vector<RenderTargetBinding>& renderTargets)
    {
        currentRenderTargets_ = renderTargets;
        renderTargetBound_ = !renderTargets.empty();
        if (!backend_) return;
        if (renderTargets.empty())
        {
            backend_->SetRenderTargets(nullptr, 0);
            return;
        }
        std::vector<CNA::Internal::Backends::IRenderTargetBackend*> backends;
        backends.reserve(renderTargets.size());
        for (const auto& binding : renderTargets)
        {
            auto* rt = dynamic_cast<RenderTarget2D*>(binding.getRenderTargetProperty());
            backends.push_back(rt ? rt->GetRenderTargetBackend() : nullptr);
        }
        backend_->SetRenderTargets(backends.data(), static_cast<int>(backends.size()));

        auto* first = dynamic_cast<RenderTarget2D*>(renderTargets[0].getRenderTargetProperty());
        if (first &&
            first->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents)
        {
            Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
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
