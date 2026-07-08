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
    };

    class SdlSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        SDL_Renderer* renderer;
        bool begun = false;
        SDL_ScaleMode scaleMode = SDL_SCALEMODE_LINEAR;

        explicit SdlSpriteBatchBackend(SDL_Renderer* renderer);
        ~SdlSpriteBatchBackend() override = default;
        void Begin() override;
        void End() override;
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
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return renderer; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;

        SDL_BlendMode blendMode_ = SDL_BLENDMODE_BLEND;

        // 3D pipeline: NOT supported by the SDL_Renderer backend.
        // @note Status: STUB. Every entry point throws std::runtime_error.
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
    };
}
