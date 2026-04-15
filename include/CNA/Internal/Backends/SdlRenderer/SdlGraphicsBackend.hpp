#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3_image/SDL_image.h>

namespace CNA::Internal::Backends::SdlRenderer {

    class SdlTextureBackend : public ITextureBackend {
    public:
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;

        SdlTextureBackend(SDL_Renderer* renderer, const std::string& assetName);
        ~SdlTextureBackend() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return texture; }
    };

    class SdlSpriteBatchBackend : public ISpriteBatchBackend {
    public:
        SDL_Renderer* renderer;
        bool begun = false;

        explicit SdlSpriteBatchBackend(SDL_Renderer* renderer);
        ~SdlSpriteBatchBackend() override = default;
        void Begin() override;
        void End() override;
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& sourceRectangle,
                  const Rectangle& destinationRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& sourceRectangle,
                  const Rectangle& destinationRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;
    };

    class SdlGraphicsBackend : public IGraphicsBackend {
    public:
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;

        SdlGraphicsBackend();
        ~SdlGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return renderer; }

        std::unique_ptr<ITextureBackend> CreateTexture(const std::string& assetName) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
    };

}
