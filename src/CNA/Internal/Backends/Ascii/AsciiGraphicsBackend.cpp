#include "CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Ascii
{
    AsciiGraphicsBackend::AsciiGraphicsBackend(const GraphicsBackendCreateArgs& args)
        : inner_(std::make_unique<SdlRenderer::SdlGraphicsBackend>(
              args.window, args.virtualWidth, args.virtualHeight,
              args.presentationMode, args.swapInterval))
    {
    }

    void AsciiGraphicsBackend::Clear(float r, float g, float b, float a) { inner_->Clear(r, g, b, a); }

    // Phase G4/G5 (not yet implemented): quantize the rendered frame into a glyph/color grid
    // before it reaches the screen. Until then, this forwards unchanged -- behaviorally identical
    // to SDL_RENDERER, matching Phase G1's "unblocks everything else" milestone.
    void AsciiGraphicsBackend::Present() { inner_->Present(); }

    void AsciiGraphicsBackend::GetViewportSize(int& width, int& height) { inner_->GetViewportSize(width, height); }
    void AsciiGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) { inner_->ReadBackbuffer(x, y, w, h, pixels); }
    void AsciiGraphicsBackend::SetVirtualResolution(int width, int height) { inner_->SetVirtualResolution(width, height); }
    void AsciiGraphicsBackend::SetPresentationMode(int mode) { inner_->SetPresentationMode(mode); }
    void AsciiGraphicsBackend::SetSwapInterval(int interval) { inner_->SetSwapInterval(interval); }
    int AsciiGraphicsBackend::ApplyMultiSampleCount(int requestedMultiSampleCount) { return inner_->ApplyMultiSampleCount(requestedMultiSampleCount); }
    SDL_Window* AsciiGraphicsBackend::GetWindowInternal() const { return inner_->GetWindowInternal(); }
    SDL_Renderer* AsciiGraphicsBackend::GetRendererInternal() const { return inner_->GetRendererInternal(); }

    std::unique_ptr<ITextureBackend> AsciiGraphicsBackend::CreateTexture(const ImageData& data) { return inner_->CreateTexture(data); }
    std::unique_ptr<ISpriteBatchBackend> AsciiGraphicsBackend::CreateSpriteBatch() { return inner_->CreateSpriteBatch(); }
    std::unique_ptr<IRenderTargetBackend> AsciiGraphicsBackend::CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                                      bool preserveContents,
                                                                                      bool mipMap,
                                                                                      int multiSampleCount)
    {
        return inner_->CreateRenderTarget2D(w, h, depthFormat, preserveContents, mipMap, multiSampleCount);
    }
    void AsciiGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt) { inner_->SetRenderTarget2D(rt); }
    void AsciiGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count) { inner_->SetRenderTargets(rts, count); }
    void AsciiGraphicsBackend::SetScissorRect(int x, int y, int w, int h) { inner_->SetScissorRect(x, y, w, h); }
    void AsciiGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc)
    {
        inner_->ApplyBlendState(colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
    }

    bool AsciiGraphicsBackend::SupportsDepthStencil() const { return inner_->SupportsDepthStencil(); }

    void AsciiGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth) { inner_->ClearColorAndDepth(r, g, b, a, depth); }
    void AsciiGraphicsBackend::ClearDepth(float depth) { inner_->ClearDepth(depth); }
    void AsciiGraphicsBackend::ClearStencil(int stencil) { inner_->ClearStencil(stencil); }
    void AsciiGraphicsBackend::ClearDepthAndStencil(float depth, int stencil) { inner_->ClearDepthAndStencil(depth, stencil); }
    void AsciiGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil) { inner_->ClearColorAndStencil(r, g, b, a, stencil); }
    void AsciiGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) { inner_->ClearColorDepthAndStencil(r, g, b, a, depth, stencil); }
    void AsciiGraphicsBackend::SetDepthTestEnabled(bool enabled) { inner_->SetDepthTestEnabled(enabled); }
    void AsciiGraphicsBackend::SetBlendEnabled(bool enabled) { inner_->SetBlendEnabled(enabled); }
    void AsciiGraphicsBackend::SetDepthWriteEnabled(bool enabled) { inner_->SetDepthWriteEnabled(enabled); }
    std::unique_ptr<IVertexBufferBackend> AsciiGraphicsBackend::CreateVertexBuffer(int vertex_capacity) { return inner_->CreateVertexBuffer(vertex_capacity); }
    std::unique_ptr<IIndexBufferBackend> AsciiGraphicsBackend::CreateIndexBuffer16(int index_capacity) { return inner_->CreateIndexBuffer16(index_capacity); }
    std::unique_ptr<IOcclusionQueryBackend> AsciiGraphicsBackend::CreateOcclusionQuery() { return inner_->CreateOcclusionQuery(); }
    void AsciiGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount)
    {
        inner_->DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }
    void AsciiGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                             const IIndexBufferBackend& ib,
                                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                                             PrimitiveType primitive, int primitiveCount)
    {
        inner_->DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_ASCII
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Ascii::AsciiGraphicsBackend>(args);
    }
#endif
}
