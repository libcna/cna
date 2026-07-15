#include "CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Ascii/AsciiFontAtlas.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

#include <stdexcept>
#include <vector>

namespace CNA::Internal::Backends::Ascii
{
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::BlendFunction;

    AsciiGraphicsBackend::AsciiGraphicsBackend(const GraphicsBackendCreateArgs& args)
        : inner_(std::make_unique<SdlRenderer::SdlGraphicsBackend>(
              args.window, args.virtualWidth, args.virtualHeight,
              args.presentationMode, args.swapInterval))
        , mode_(ParseAsciiModeFromEnvironment())
    {
        presentSpriteBatch_ = inner_->CreateSpriteBatch();
        fontAtlasTexture_ = inner_->CreateTexture(BuildAsciiFontAtlasImageData());
        RecreateGameTarget(args.virtualWidth, args.virtualHeight);
    }

    void AsciiGraphicsBackend::RecreateGameTarget(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
        gameTarget_ = inner_->CreateRenderTarget2D(width, height, /*depthFormat=*/0,
                                                    /*preserveContents=*/false, /*mipMap=*/false,
                                                    /*multiSampleCount=*/0);
        inner_->SetRenderTarget2D(gameTarget_.get());
    }

    void AsciiGraphicsBackend::Clear(float r, float g, float b, float a) { inner_->Clear(r, g, b, a); }

    void AsciiGraphicsBackend::SetCellSize(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw std::invalid_argument("CNA Ascii: cell size must be positive");
        }
        cellWidth_ = width;
        cellHeight_ = height;
    }

    void AsciiGraphicsBackend::GetCellSize(int& width, int& height) const
    {
        width = cellWidth_;
        height = cellHeight_;
    }

    // Shared by Present() and DrawQuantizedGridForTesting(): reads gameTarget_ back while it's
    // still bound (ReadBackbuffer reads whatever's current), quantizes that into a glyph/color
    // grid, switches to the real backbuffer, and draws the grid there -- one tinted textured
    // quad per cell (background fill, if any, then the glyph on top), reusing the same
    // internal-only presentSpriteBatch_ the Phase G3 plain blit used. Deliberately does NOT call
    // inner_->Present() or rebind gameTarget_ -- callers do that themselves, since a real
    // double-buffer swap invalidates immediate readback (see DrawQuantizedGridForTesting's own
    // doc comment for why this split exists at all).
    void AsciiGraphicsBackend::DrawQuantizedGridOntoRealBackbuffer()
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(virtualWidth_) * static_cast<std::size_t>(virtualHeight_) * 4);
        inner_->ReadBackbuffer(0, 0, virtualWidth_, virtualHeight_, pixels.data());

        const AsciiGrid grid = QuantizeFrameToGrid(pixels.data(), virtualWidth_, virtualHeight_,
                                                   cellWidth_, cellHeight_, mode_);
        lastGridColumns_ = grid.columns;
        lastGridRows_ = grid.rows;

        inner_->SetRenderTarget2D(nullptr);

        int realWidth = 0, realHeight = 0;
        inner_->GetViewportSize(realWidth, realHeight);

        inner_->Clear(0.0f, 0.0f, 0.0f, 1.0f);

        // presentSpriteBatch_ never goes through GraphicsDevice::BlendState (only real XNA-level
        // SpriteBatch::Begin() calls do that) -- confirmed empirically (Ascii_Present ctest) that
        // without this, the renderer's blend mode is whatever it happened to default to, which
        // isn't guaranteed to be alpha-aware, and glyph "off" pixels (transparent) were seen
        // silently overwriting the background fill with the texture's stored RGB (black) instead
        // of leaving it alone. Force real premultiplied AlphaBlend (XNA's own BlendState.AlphaBlend
        // factors) before drawing, every time, independent of whatever the game's own BlendState is.
        inner_->ApplyBlendState(static_cast<int>(Blend::One), static_cast<int>(Blend::One),
                                static_cast<int>(Blend::InverseSourceAlpha), static_cast<int>(Blend::InverseSourceAlpha),
                                static_cast<int>(BlendFunction::Add), static_cast<int>(BlendFunction::Add));

        const Rectangle solidSrc(kAsciiSolidGlyphIndex * kAsciiGlyphWidth, 0, kAsciiGlyphWidth, kAsciiGlyphHeight);

        presentSpriteBatch_->Begin();
        for (int row = 0; row < grid.rows; ++row)
        {
            // Both edges of each cell are computed directly from row/col (not accumulated by
            // adding a per-cell width/height repeatedly), so adjacent cells' shared edge always
            // matches exactly -- no rounding-induced gaps or overlaps between cells.
            const int cellY0 = static_cast<int>((static_cast<long long>(row) * realHeight) / grid.rows);
            const int cellY1 = static_cast<int>((static_cast<long long>(row + 1) * realHeight) / grid.rows);
            for (int col = 0; col < grid.columns; ++col)
            {
                const int cellX0 = static_cast<int>((static_cast<long long>(col) * realWidth) / grid.columns);
                const int cellX1 = static_cast<int>((static_cast<long long>(col + 1) * realWidth) / grid.columns);
                const Rectangle dest(cellX0, cellY0, cellX1 - cellX0, cellY1 - cellY0);

                const AsciiCell& cell = grid.At(col, row);
                if (cell.hasBackground)
                {
                    presentSpriteBatch_->Draw(*fontAtlasTexture_, dest, solidSrc, cell.background);
                }

                const Rectangle glyphSrc(cell.glyphIndex * kAsciiGlyphWidth, 0, kAsciiGlyphWidth, kAsciiGlyphHeight);
                presentSpriteBatch_->Draw(*fontAtlasTexture_, dest, glyphSrc, cell.foreground);
            }
        }
        presentSpriteBatch_->End();
    }

    void AsciiGraphicsBackend::Present()
    {
        DrawQuantizedGridOntoRealBackbuffer();
        inner_->Present();
        inner_->SetRenderTarget2D(gameTarget_.get());
    }

    // A real double-buffer swap (the inner_->Present() call above) can genuinely invalidate
    // immediate readback of what was just drawn -- confirmed empirically (Ascii_Present ctest):
    // reading right after a real Present() returned all-black, because SDL_Renderer/OpenGL
    // presents by swapping buffers, not copying, so "the current render target" right after a
    // swap is the *other*, not-yet-drawn-this-frame buffer. This method stops right after
    // drawing, before any swap, so ReadRealBackbufferForTesting() called immediately afterward
    // reads the buffer that was actually just drawn into -- exposed only for testing; real game
    // code must always go through the real Present() instead.
    void AsciiGraphicsBackend::DrawQuantizedGridForTesting()
    {
        DrawQuantizedGridOntoRealBackbuffer();
    }

    void AsciiGraphicsBackend::GetLastGridDimensionsForTesting(int& columns, int& rows) const
    {
        columns = lastGridColumns_;
        rows = lastGridRows_;
    }

    void AsciiGraphicsBackend::ReadRealBackbufferForTesting(int x, int y, int w, int h, uint8_t* pixels)
    {
        inner_->SetRenderTarget2D(nullptr);
        inner_->ReadBackbuffer(x, y, w, h, pixels);
        inner_->SetRenderTarget2D(gameTarget_.get());
    }

    void AsciiGraphicsBackend::GetViewportSize(int& width, int& height) { inner_->GetViewportSize(width, height); }
    void AsciiGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) { inner_->ReadBackbuffer(x, y, w, h, pixels); }
    // Phase G3: gameTarget_ is sized to the game's own logical/virtual resolution, independent
    // of the real window's physical size -- a resolution change (unlike a real-window resize,
    // which SDL's own logical-presentation scaling already absorbs transparently) genuinely needs
    // a new offscreen target at the new size.
    void AsciiGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        inner_->SetVirtualResolution(width, height);
        RecreateGameTarget(width, height);
    }
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
    // Phase G3: XNA's "target the back buffer" idiom (a null target) is redirected to gameTarget_
    // instead of being forwarded as a literal nullptr -- the game must never be able to draw
    // straight onto the real window, only onto its own offscreen target (design decision 2/3).
    // A genuinely non-null target (the game's own RenderTarget2D) is forwarded unchanged.
    void AsciiGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        inner_->SetRenderTarget2D(rt != nullptr ? rt : gameTarget_.get());
    }
    void AsciiGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (count == 0)
        {
            inner_->SetRenderTarget2D(gameTarget_.get());
        }
        else
        {
            inner_->SetRenderTargets(rts, count);
        }
    }
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
