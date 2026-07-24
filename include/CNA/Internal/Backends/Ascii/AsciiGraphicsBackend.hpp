#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "../SdlRenderer/SdlGraphicsBackend.hpp"
#include "AsciiFontAtlas.hpp"
#include "AsciiQuantizer.hpp"
#include <memory>

namespace CNA::Internal::Backends::Ascii
{
    /**
     * @brief SDL-windowed retro text/glyph-grid graphics backend ("ASCII").
     *
     * Not a real terminal/TTY backend -- see plan_ascii.md. Architecturally a thin decorator
     * around SDL_RENDERER's own SdlGraphicsBackend (design decision 2): every IGraphicsBackend
     * method not related to presentation forwards straight to a wrapped, fully real
     * SdlRenderer::SdlGraphicsBackend instance, which does all the actual compositing.
     *
     * Phase G3: the game never draws directly to the real backbuffer. A private offscreen
     * render target (gameTarget_), sized to the game's own logical/virtual resolution, is bound
     * as the default target instead -- SetRenderTarget2D(nullptr)/SetRenderTargets(..., 0) (XNA's
     * "target the back buffer" idiom) are intercepted and redirected to gameTarget_ rather than
     * forwarded as a literal nullptr, so the game can never accidentally draw straight onto the
     * real window.
     *
     * Phase G4/G5: Present() reads gameTarget_ back, quantizes it into a glyph/color grid
     * (AsciiQuantizer.hpp) using mode_, then draws that grid -- one textured, tinted quad per
     * cell from fontAtlasTexture_ (AsciiFontAtlas.hpp) -- onto the real backbuffer via the same
     * internal-only presentSpriteBatch_ the Phase G3 plain blit used, presents for real, then
     * rebinds gameTarget_ for the next frame.
     */
    class AsciiGraphicsBackend : public IGraphicsBackend
    {
    public:
        explicit AsciiGraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~AsciiGraphicsBackend() override = default;

        /// Sets the quantization mode used by Present() (design decision 5). Callable at any
        /// time, including before Game::Run(), same as HeadlessGraphicsBackend::SetMode().
        void SetMode(AsciiQuantizeMode mode) { mode_ = mode; }
        [[nodiscard]] AsciiQuantizeMode GetMode() const { return mode_; }

        /// Sets the source-pixel block size (design decision 6) that Present() hands to
        /// QuantizeFrameToGrid() every frame -- how many gameTarget_ pixels are averaged into one
        /// glyph cell, which in turn determines the grid's column/row count for a given logical
        /// resolution. Independent of the font atlas' own fixed 8x8 glyph texture size: the grid
        /// cell's on-screen quad is always stretched to fill realWidth/columns x
        /// realHeight/rows, whatever that resolves to, so a coarser/finer cell size here just
        /// changes how many cells there are, not how the atlas is sampled. Callable at any time,
        /// including before Game::Run(), same as SetMode(). Defaults to
        /// kAsciiGlyphWidth x kAsciiGlyphHeight (8x8).
        void SetCellSize(int width, int height);
        /// Retrieves the current source-pixel block size set by SetCellSize().
        void GetCellSize(int& width, int& height) const;

        /// Does everything Present() does EXCEPT the real double-buffer swap and the gameTarget_
        /// rebind -- exposed only for testing (Ascii_Present ctest). A real swap can genuinely
        /// invalidate immediate readback of what was just drawn (confirmed empirically: SDL's
        /// OpenGL-backed present is a buffer swap, not a copy), so a test that needs to verify
        /// drawn pixel content must read them before any swap happens. Real game code must always
        /// call Present() instead -- never this.
        void DrawQuantizedGridForTesting();

        /// Reports the glyph grid's column/row count as of the most recent Present()/
        /// DrawQuantizedGridForTesting() call -- exposed only for testing (Ascii_Present ctest),
        /// so a test can confirm SetCellSize() actually changed what Present() draws, not just
        /// that the setter/getter round-trip.
        void GetLastGridDimensionsForTesting(int& columns, int& rows) const;

        /// Reads the REAL backbuffer (not gameTarget_) -- exposed only for testing
        /// (Ascii_Present ctest), normally called right after DrawQuantizedGridForTesting().
        /// Normal game code always reads gameTarget_ via the ordinary
        /// ReadBackbuffer()/GetBackBufferData() path.
        void ReadRealBackbufferForTesting(int x, int y, int w, int h, uint8_t* pixels);

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        SDL_Window* GetWindowInternal() const override;
        SDL_Renderer* GetRendererInternal() const override;

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                              int colorDstBlend, int alphaDstBlend,
                              int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /// No depth/stencil buffer on this backend either -- inherited from the same underlying
        /// 2D-only reality SDL_RENDERER already has (design decision 9's Phase G7 reuse).
        [[nodiscard]] bool SupportsDepthStencil() const override;

        // 3D pipeline: NOT supported, same as the wrapped SDL_RENDERER backend. Forwards to
        // SdlGraphicsBackend's own ThrowNo3D-driven implementations rather than re-declaring them
        // (plan_ascii.md design decision 10 / Phase G7).
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

    private:
        /// The real, wrapped SDL_RENDERER backend instance that does all the actual compositing
        /// (design decision 2). Never exposed outside this class.
        std::unique_ptr<SdlRenderer::SdlGraphicsBackend> inner_;

        /// Offscreen target the game actually draws to (Phase G3). Sized to the game's own
        /// logical/virtual resolution, independent of the real window's physical size.
        std::unique_ptr<IRenderTargetBackend> gameTarget_;
        /// Internal-only sprite batch used by Present() to draw onto the real backbuffer --
        /// never exposed to game code (which gets its own via CreateSpriteBatch()).
        std::unique_ptr<ISpriteBatchBackend> presentSpriteBatch_;
        /// The Phase G2 glyph atlas, uploaded once at construction as a plain backend texture
        /// (not a SpriteFont -- Present() has no GraphicsDevice to build one with; see
        /// AsciiFontAtlas::BuildAsciiFontAtlasImageData()'s own doc comment).
        std::unique_ptr<ITextureBackend> fontAtlasTexture_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        /// Quantization mode, parsed from CNA_ASCII_MODE at construction (design decision 5),
        /// overridable at any time via SetMode().
        AsciiQuantizeMode mode_ = AsciiQuantizeMode::Color;
        /// Source-pixel block size Present() quantizes with (design decision 6), overridable at
        /// any time via SetCellSize(). Defaults to the font atlas' own glyph size.
        int cellWidth_ = kAsciiGlyphWidth;
        int cellHeight_ = kAsciiGlyphHeight;
        /// Grid dimensions from the most recent DrawQuantizedGridOntoRealBackbuffer() call --
        /// exposed via GetLastGridDimensionsForTesting() only.
        int lastGridColumns_ = 0;
        int lastGridRows_ = 0;

        /// (Re)creates gameTarget_ at the given size and binds it as the current target.
        void RecreateGameTarget(int width, int height);

        /// Shared by Present() and DrawQuantizedGridForTesting(): reads gameTarget_, quantizes
        /// it, switches to the real backbuffer, and draws the grid there. Does not swap or
        /// rebind gameTarget_ -- callers do that themselves.
        void DrawQuantizedGridOntoRealBackbuffer();
    };
}
