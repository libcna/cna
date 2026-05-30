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
    };

    class SdlSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        SDL_Renderer* renderer;
        bool begun = false;

        explicit SdlSpriteBatchBackend(SDL_Renderer* renderer);
        ~SdlSpriteBatchBackend() override = default;
        void Begin() override;
        void End() override;
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
                           CnaPresentationMode mode = CnaPresentationMode::Overscan);
        ~SdlGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return renderer; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        // 3D pipeline: NOT supported by the SDL_Renderer backend.
        // @note Status: STUB. Every entry point throws std::runtime_error.
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void SetDepthTestEnabled(bool enabled) override;
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
