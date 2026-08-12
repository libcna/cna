#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

namespace CNA::Internal::Renderers::SdlRenderer
{
    /** @brief SDL-only texture access used inside the SDL_Renderer backend. */
    class ISdlTextureRenderer
    {
    public:
        /** @brief Destroys the SDL texture view. */
        virtual ~ISdlTextureRenderer() = default;

        /**
         * @brief Gets the SDL texture owned by this backend resource.
         * @return The non-owning SDL texture pointer used by the SDL sprite batch.
         */
        [[nodiscard]] virtual SDL_Texture* GetNativeSdlTexture() const = 0;
    };

    class SdlTextureRenderer : public ITextureRenderer, public ISdlTextureRenderer
    {
    public:
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;

        SdlTextureRenderer(SDL_Renderer* renderer, const ImageData& data);
        ~SdlTextureRenderer() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeSdlTexture() const override { return texture; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
    };

    /// SDL_TEXTUREACCESS_TARGET-based off-screen render target.
    class SdlRenderTargetRenderer : public IRenderTargetRenderer, public ISdlTextureRenderer
    {
    public:
        SDL_Texture* texture = nullptr;
        SDL_Renderer* renderer = nullptr;
        int width = 0;
        int height = 0;

        SdlRenderTargetRenderer(SDL_Renderer* r, int w, int h);
        ~SdlRenderTargetRenderer() override;

        int GetWidth()  const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeSdlTexture() const override { return texture; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void BindGL(int /*unit*/) const override {}

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127. SDL_Renderer has a real, fully synchronous readback for a
         * `SDL_TEXTUREACCESS_TARGET` texture: make it the render target and call
         * `SDL_RenderReadPixels`, which flushes the pending command queue itself. Before this
         * override existed the call reached `ITextureRenderer::GetData`'s default, and the shared
         * layer converted its own zero-initialized scratch buffer into the caller's array -- a
         * fabricated, fully written transparent-black frame rather than an honest refusal.
         *
         * The renderer's previous target is restored before returning, so a readback never changes
         * what subsequent drawing goes to. No staging texture is kept: the surface SDL returns is
         * created and destroyed inside this call, so repeated readbacks hold no extra memory.
         *
         * @param level      Mip level; SDL_Renderer render targets have no mip chain.
         * @param x          Left edge of the requested rectangle, in target pixels.
         * @param y          Top edge of the requested rectangle, in target pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false if SDL could not complete
         *         the readback, in which case @p data is left untouched.
         * @throws System::NotSupportedException if @p level is above 0.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the target, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;

        // Task 708: SDL_Renderer's 2D-only render targets never allocate a real depth-stencil
        // buffer, regardless of what DepthFormat was requested at construction time (see
        // CreateRenderTarget2D, which ignores its depthFormat parameter entirely) -- so this
        // always reports false rather than trusting the merely-requested XNA-level format.
        bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }
    };

    class SdlSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        SDL_Renderer* renderer;
        bool begun = false;
        SDL_ScaleMode scaleMode = SDL_SCALEMODE_LINEAR;
        // Task 675: transform matrix applied on top of each sprite's own destRect/rotation/origin
        // placement. Defaults to Identity -- SDL_RenderTextureRotated() is used unchanged
        // whenever this stays Identity (the overwhelmingly common case), so this fix carries zero
        // regression risk for existing rotation/flip/scale/source-rect behaviour; only a
        // genuinely non-Identity transform routes through the new SDL_RenderTextureAffine() path.
        Matrix transformMatrix = Matrix::getIdentityProperty();

        explicit SdlSpriteBatchRenderer(SDL_Renderer* renderer);
        ~SdlSpriteBatchRenderer() override = default;
        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transformMatrix = m; }
        // Task 676: SDL_Renderer has no programmable shader stage at all, so a custom Effect
        // (SpriteBatch::Begin's effect parameter) can never actually be applied -- silently
        // ignoring it would misrender any game that depends on the custom shader's visual output.
        // Throws whenever a non-null Effect is supplied; a null Effect (the default, "no custom
        // effect" case used by every other SpriteBatch call in this project) is always a no-op,
        // matching SpriteBatch::Begin's own unconditional per-Begin() call to this method.
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;
    };

    class SdlRenderer : public IGraphicsRenderer
    {
    public:
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        int logicalWidth = 0;
        int logicalHeight = 0;
        int lastOutputW_ = 0; ///< last known renderer output width; used to detect Android surface resize
        int lastOutputH_ = 0; ///< last known renderer output height
        CnaPresentationMode presentationMode_ = CnaPresentationMode::Overscan;

        SdlRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                           CnaPresentationMode mode = CnaPresentationMode::Overscan,
                           int swapInterval = 1);
        ~SdlRenderer() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        // Task 666: real SDL_RenderReadPixels-based backbuffer/render-target readback --
        // GraphicsDevice::GetBackBufferData was previously a hard throw on this renderer (the
        // shared IGraphicsRenderer::ReadBackbuffer default), blocking every pixel-verification
        // test this project's methodology relies on.
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        // Task 714: SDL_Renderer's 2D blit pipeline has no MSAA control at all -- accepts any
        // requested MultiSampleCount without throwing (logging once per request), always
        // reporting back 0 (the real, device-clamped maximum on this renderer).
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return renderer; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        // Task 709: SDL_Renderer supports exactly one active render target at a time -- the
        // shared IGraphicsRenderer::SetRenderTargets default would otherwise silently bind only
        // rts[0] and ignore the rest. Throws clearly for count > 1 instead.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        SDL_BlendMode blendMode_ = SDL_BLENDMODE_BLEND;

        // 3D pipeline: NOT supported by the SDL_Renderer renderer. Calls preserve their
        // established throw/null behavior by default, or warn once and return a safe
        // no-op/null object when the caller explicitly selects
        // Unsupported3DGraphicsCallBehavior::WarnAndStub.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // The renderer is 2D-only, but SDL_ComposeCustomBlendMode represents the standard
            // Additive preset with independent colour/alpha factors and operations exactly.
            return capability == CNA::GraphicsCapability::AdditiveBlending;
        }
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        // Task 727: the shared IGraphicsRenderer::CreateOcclusionQuery default silently returns
        // nullptr (no throw) -- OcclusionQuery::Begin/End then silently no-op instead of ever
        // running a real occlusion query. This override throws in the default policy and returns
        // a completed zero-pixel null query in WarnAndStub.
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
    };
}
