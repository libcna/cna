// plan_dx.md Phase DX12 (DX-101): D3D12 backend skeleton -- CMake wiring only, no real D3D12 API
// calls yet. See D3D12GraphicsBackend.hpp for why every method below throws.
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::D3D12
{
    void D3D12GraphicsBackend::NotYetImplemented(const char* what)
    {
        throw std::runtime_error(std::string("D3D12 backend: ") + what +
                                  " not yet implemented (plan_dx.md DX-102 onward)");
    }

    D3D12GraphicsBackend::D3D12GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
    {
        SDL_Log("[D3D12] Backend skeleton constructed (plan_dx.md DX-101) -- "
                "device/swap-chain/draw paths are not yet implemented.");
    }

    D3D12GraphicsBackend::~D3D12GraphicsBackend() = default;

    void D3D12GraphicsBackend::Clear(float, float, float, float) { NotYetImplemented("Clear"); }
    void D3D12GraphicsBackend::Present() { NotYetImplemented("Present"); }

    void D3D12GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void D3D12GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void D3D12GraphicsBackend::SetPresentationMode(int) { /* no-op until DX-102 */ }

    std::unique_ptr<ITextureBackend> D3D12GraphicsBackend::CreateTexture(const ImageData&)
    {
        NotYetImplemented("CreateTexture");
    }

    std::unique_ptr<ISpriteBatchBackend> D3D12GraphicsBackend::CreateSpriteBatch()
    {
        NotYetImplemented("CreateSpriteBatch");
    }

    void D3D12GraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { NotYetImplemented("ClearColorAndDepth"); }
    void D3D12GraphicsBackend::ClearDepth(float) { NotYetImplemented("ClearDepth"); }
    void D3D12GraphicsBackend::ClearStencil(int) { NotYetImplemented("ClearStencil"); }
    void D3D12GraphicsBackend::ClearDepthAndStencil(float, int) { NotYetImplemented("ClearDepthAndStencil"); }
    void D3D12GraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { NotYetImplemented("ClearColorAndStencil"); }
    void D3D12GraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { NotYetImplemented("ClearColorDepthAndStencil"); }

    void D3D12GraphicsBackend::SetDepthTestEnabled(bool) { NotYetImplemented("SetDepthTestEnabled"); }
    void D3D12GraphicsBackend::SetBlendEnabled(bool) { NotYetImplemented("SetBlendEnabled"); }
    void D3D12GraphicsBackend::SetDepthWriteEnabled(bool) { NotYetImplemented("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> D3D12GraphicsBackend::CreateVertexBuffer(int)
    {
        NotYetImplemented("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> D3D12GraphicsBackend::CreateIndexBuffer16(int)
    {
        NotYetImplemented("CreateIndexBuffer16");
    }

    void D3D12GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                      const Matrix&, const Matrix&, const Matrix&,
                                                      PrimitiveType, int)
    {
        NotYetImplemented("DrawColoredPrimitives");
    }

    void D3D12GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                             const Matrix&, const Matrix&, const Matrix&,
                                                             PrimitiveType, int)
    {
        NotYetImplemented("DrawIndexedColoredPrimitives");
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D12::D3D12GraphicsBackend>(args);
    }
}
