#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "../SdlRenderer/SdlGraphicsBackend.hpp"
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
     * real window. Present() unbinds gameTarget_, draws it (Phase G3: a plain stretch-blit; Phase
     * G4/G5: the quantized glyph grid instead) onto the real backbuffer, presents for real, then
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
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                              int colorDstBlend, int alphaDstBlend,
                              int colorBlendFunc, int alphaBlendFunc) override;

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
        /// Internal-only sprite batch used by Present() to draw gameTarget_ onto the real
        /// backbuffer -- never exposed to game code (which gets its own via CreateSpriteBatch()).
        std::unique_ptr<ISpriteBatchBackend> presentSpriteBatch_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        /// Quantization mode, parsed from CNA_ASCII_MODE at construction (design decision 5),
        /// overridable at any time via SetMode().
        AsciiQuantizeMode mode_ = AsciiQuantizeMode::Color;

        /// (Re)creates gameTarget_ at the given size and binds it as the current target.
        void RecreateGameTarget(int width, int height);
    };
}
