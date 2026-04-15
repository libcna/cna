#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include <string>
#include <memory>
#include "CNA/Internal/Graphics/ImageData.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace CNA::Internal::Backends {

    using Color = Microsoft::Xna::Framework::Color;
    using Rectangle = Microsoft::Xna::Framework::Rectangle;
    using Vector2 = Microsoft::Xna::Framework::Vector2;
    using SpriteEffects = Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using ImageData = CNA::Internal::Graphics::ImageData;

    class ITextureBackend {
    public:
        virtual ~ITextureBackend() = default;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Texture* GetNativeTexture() const = 0;
    };

    class ISpriteBatchBackend {
    public:
        virtual ~ISpriteBatchBackend() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void Draw(const ITextureBackend& texture, float x, float y) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& sourceRectangle,
                          const Rectangle& destinationRectangle,
                          const Color& color) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& sourceRectangle,
                          const Rectangle& destinationRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth) = 0;
    };

    class IGraphicsBackend {
    public:
        virtual ~IGraphicsBackend() = default;
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void Present() = 0;
        virtual void GetViewportSize(int& width, int& height) = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Window* GetWindowInternal() const = 0;
        virtual SDL_Renderer* GetRendererInternal() const = 0;

        virtual std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) = 0;
        virtual std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() = 0;
    };

    /**
     * @brief Arguments for creating a graphics backend.
     * Currently minimal, but allows for easier extension.
     */
    struct GraphicsBackendCreateArgs {
        // TODO: SDL dependency should be abstracted later
        SDL_Window* window = nullptr;
    };

    // Factory function to be implemented by each backend
    // INTERNAL API - SDL dependency should be abstracted later
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args);

}
