// plan_dx9.md Phase D9-1: D3D9 backend skeleton.
#include "CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp"
#include "CNA/Internal/Backends/Common/NotYetImplemented.hpp"

#include <SDL3/SDL.h>

namespace CNA::Internal::Backends::D3D9
{
    D3D9GraphicsBackend::D3D9GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(static_cast<int>(args.presentationMode))
    {
        if (window_)
        {
            SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        }
        if (width_ <= 0) width_ = args.virtualWidth > 0 ? args.virtualWidth : 1024;
        if (height_ <= 0) height_ = args.virtualHeight > 0 ? args.virtualHeight : 768;

        SDL_Log("[D3D9] Backend skeleton constructed (%dx%d) -- no device yet, see plan_dx9.md D9-30",
                width_, height_);
    }

    D3D9GraphicsBackend::~D3D9GraphicsBackend() = default;

    void D3D9GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = width_;
        height = height_;
    }

    void D3D9GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void D3D9GraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = mode;
    }

    void D3D9GraphicsBackend::Clear(float, float, float, float)
    {
        NotYetImplemented("D3D9", "Clear (no device yet -- see plan_dx9.md D9-30/D9-31)");
    }

    void D3D9GraphicsBackend::Present()
    {
        NotYetImplemented("D3D9", "Present (no device yet -- see plan_dx9.md D9-30/D9-31)");
    }

    void D3D9GraphicsBackend::ClearColorAndDepth(float, float, float, float, float)
    {
        NotYetImplemented("D3D9", "ClearColorAndDepth (see plan_dx9.md D9-31)");
    }

    void D3D9GraphicsBackend::ClearDepth(float)
    {
        NotYetImplemented("D3D9", "ClearDepth (see plan_dx9.md D9-31)");
    }

    void D3D9GraphicsBackend::ClearStencil(int)
    {
        NotYetImplemented("D3D9", "ClearStencil (see plan_dx9.md D9-31)");
    }

    void D3D9GraphicsBackend::ClearDepthAndStencil(float, int)
    {
        NotYetImplemented("D3D9", "ClearDepthAndStencil (see plan_dx9.md D9-31)");
    }

    void D3D9GraphicsBackend::ClearColorAndStencil(float, float, float, float, int)
    {
        NotYetImplemented("D3D9", "ClearColorAndStencil (see plan_dx9.md D9-31)");
    }

    void D3D9GraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int)
    {
        NotYetImplemented("D3D9", "ClearColorDepthAndStencil (see plan_dx9.md D9-31)");
    }

    void D3D9GraphicsBackend::SetDepthTestEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetDepthTestEnabled (see plan_dx9.md D9-61/D9-82)");
    }

    void D3D9GraphicsBackend::SetBlendEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetBlendEnabled (see plan_dx9.md D9-60/D9-82)");
    }

    void D3D9GraphicsBackend::SetDepthWriteEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetDepthWriteEnabled (see plan_dx9.md D9-61/D9-82)");
    }

    std::unique_ptr<IVertexBufferBackend> D3D9GraphicsBackend::CreateVertexBuffer(int)
    {
        NotYetImplemented("D3D9", "CreateVertexBuffer (see plan_dx9.md D9-40)");
    }

    std::unique_ptr<IIndexBufferBackend> D3D9GraphicsBackend::CreateIndexBuffer16(int)
    {
        NotYetImplemented("D3D9", "CreateIndexBuffer16 (see plan_dx9.md D9-41)");
    }

    void D3D9GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                     const Matrix&, const Matrix&, const Matrix&,
                                                     PrimitiveType, int)
    {
        NotYetImplemented("D3D9", "DrawColoredPrimitives (see plan_dx9.md D9-82)");
    }

    void D3D9GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                            const Matrix&, const Matrix&, const Matrix&,
                                                            PrimitiveType, int)
    {
        NotYetImplemented("D3D9", "DrawIndexedColoredPrimitives (see plan_dx9.md D9-82)");
    }

    std::unique_ptr<ITextureBackend> D3D9GraphicsBackend::CreateTexture(const ImageData&)
    {
        NotYetImplemented("D3D9", "CreateTexture (see plan_dx9.md D9-50)");
    }

    std::unique_ptr<ISpriteBatchBackend> D3D9GraphicsBackend::CreateSpriteBatch()
    {
        NotYetImplemented("D3D9", "CreateSpriteBatch (see plan_dx9.md D9-90)");
    }

    void D3D9GraphicsBackend::SetSwapInterval(int)
    {
        NotYetImplemented("D3D9", "SetSwapInterval (see plan_dx9.md D9-30)");
    }

    void D3D9GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend*)
    {
        NotYetImplemented("D3D9", "SetRenderTarget2D (see plan_dx9.md D9-53)");
    }

    void D3D9GraphicsBackend::SetBlendFactor(float, float, float, float)
    {
        NotYetImplemented("D3D9", "SetBlendFactor (see plan_dx9.md D9-60)");
    }

    void D3D9GraphicsBackend::SetReferenceStencil(int)
    {
        NotYetImplemented("D3D9", "SetReferenceStencil (see plan_dx9.md D9-61)");
    }

    void D3D9GraphicsBackend::SetScissorRect(int, int, int, int)
    {
        NotYetImplemented("D3D9", "SetScissorRect (see plan_dx9.md D9-62)");
    }

    void D3D9GraphicsBackend::SetViewport(int, int, int, int, float, float)
    {
        NotYetImplemented("D3D9", "SetViewport (see plan_dx9.md D9-62)");
    }

    void D3D9GraphicsBackend::SetContextRecoveryEnabled(bool)
    {
        NotYetImplemented("D3D9", "SetContextRecoveryEnabled");
    }

    void D3D9GraphicsBackend::SetStringMarkerEXT(const char*)
    {
        NotYetImplemented("D3D9", "SetStringMarkerEXT");
    }

    void D3D9GraphicsBackend::DebugSimulateContextLoss()
    {
        NotYetImplemented("D3D9", "DebugSimulateContextLoss (see plan_dx9.md D9-34)");
    }

    void D3D9GraphicsBackend::DebugRestoreContext()
    {
        NotYetImplemented("D3D9", "DebugRestoreContext (see plan_dx9.md D9-34)");
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D9::D3D9GraphicsBackend>(args);
    }
}
