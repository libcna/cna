#pragma once

#include "../common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class EasyGLTextureBackend : public ITextureBackend {
    public:
        EasyGLTextureBackend(const std::string& assetName);
        int GetWidth() const override { return 0; }
        int GetHeight() const override { return 0; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
    };

    class EasyGLSpriteBatchBackend : public ISpriteBatchBackend {
    public:
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

    class EasyGLGraphicsBackend : public IGraphicsBackend {
    public:
        EasyGLGraphicsBackend();
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        SDL_Window* GetWindowInternal() const override { return nullptr; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const std::string& assetName) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
    };

}
