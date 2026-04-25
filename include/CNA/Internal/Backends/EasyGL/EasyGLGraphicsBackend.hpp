#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include <SDL3/SDL.h>
#include <easygl/easygl.hpp>

namespace CNA::Internal::Backends::EasyGL {

    class EasyGLTextureBackend : public ITextureBackend {
    public:
        ::easygl::Texture texture;
        int width = 0;
        int height = 0;

        EasyGLTextureBackend(const ImageData& data);
        ~EasyGLTextureBackend() override = default;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
    };

    class EasyGLSpriteBatchBackend : public ISpriteBatchBackend {
    private:
        ::easygl::Device& device_;
        ::easygl::Program program_;
        ::easygl::VertexArray vao_;
        ::easygl::Buffer vbo_;
        ::easygl::Buffer ibo_;
        bool begun = false;

    public:
        explicit EasyGLSpriteBatchBackend(::easygl::Device& device);
        ~EasyGLSpriteBatchBackend() override = default;

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

    private:
        void InitializeResources();
    };

    class EasyGLGraphicsBackend : public IGraphicsBackend {
    private:
        SDL_Window* window = nullptr;
        SDL_GLContext gl_context = nullptr;
        ::easygl::Device device;

    public:
        explicit EasyGLGraphicsBackend(SDL_Window* window);
        ~EasyGLGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
    };

}
