#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

namespace CNA::Internal::Backends::SdlRenderer
{
    class SdlTextureBackend : public ITextureBackend
    {
    public:
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;

        SdlTextureBackend(SDL_Renderer* renderer, const ImageData& data);
        ~SdlTextureBackend() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return texture; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
    };

    /// SDL_TEXTUREACCESS_TARGET-based off-screen render target.
    class SdlRenderTargetBackend : public IRenderTargetBackend
    {
    public:
        SDL_Texture* texture = nullptr;
        SDL_Renderer* renderer = nullptr;
        int width = 0;
        int height = 0;

        SdlRenderTargetBackend(SDL_Renderer* r, int w, int h);
        ~SdlRenderTargetBackend() override;

        int GetWidth()  const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return texture; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void BindGL() const override {}

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;

        // Task 708: SDL_Renderer's 2D-only render targets never allocate a real depth-stencil
        // buffer, regardless of what DepthFormat was requested at construction time (see
        // CreateRenderTarget2D, which ignores its depthFormat parameter entirely) -- so this
        // always reports false rather than trusting the merely-requested XNA-level format.
        bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }
    };

    class SdlSpriteBatchBackend : public ISpriteBatchBackend
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

        explicit SdlSpriteBatchBackend(SDL_Renderer* renderer);
        ~SdlSpriteBatchBackend() override = default;
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
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;
    };

    class SdlGraphicsBackend : public IGraphicsBackend
    {
    public:
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        int logicalWidth = 0;
        int logicalHeight = 0;
        int lastOutputW_ = 0; ///< last known renderer output width; used to detect Android surface resize
        int lastOutputH_ = 0; ///< last known renderer output height
        CnaPresentationMode presentationMode_ = CnaPresentationMode::Overscan;

        SdlGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                           CnaPresentationMode mode = CnaPresentationMode::Overscan,
                           int swapInterval = 1);
        ~SdlGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        // Task 666: real SDL_RenderReadPixels-based backbuffer/render-target readback --
        // GraphicsDevice::GetBackBufferData was previously a hard throw on this backend (the
        // shared IGraphicsBackend::ReadBackbuffer default), blocking every pixel-verification
        // test this project's methodology relies on.
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        // Task 714: SDL_Renderer's 2D blit pipeline has no MSAA control at all -- accepts any
        // requested MultiSampleCount without throwing (logging once per request), always
        // reporting back 0 (the real, device-clamped maximum on this backend).
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return renderer; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        // Task 709: SDL_Renderer supports exactly one active render target at a time -- the
        // shared IGraphicsBackend::SetRenderTargets default would otherwise silently bind only
        // rts[0] and ignore the rest. Throws clearly for count > 1 instead.
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;

        SDL_BlendMode blendMode_ = SDL_BLENDMODE_BLEND;

        // 3D pipeline: NOT supported by the SDL_Renderer backend.
        // @note Status: STUB. Every entry point throws std::runtime_error.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
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
        // Task 727: the shared IGraphicsBackend::CreateOcclusionQuery default silently returns
        // nullptr (no throw) -- OcclusionQuery::Begin/End then silently no-op instead of ever
        // running a real occlusion query, unlike every other 3D-only entry point on this backend
        // (which all throw loudly and immediately). Throw here too for consistency.
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
    };
}
