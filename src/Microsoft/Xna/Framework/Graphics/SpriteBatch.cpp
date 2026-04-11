#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

namespace Microsoft::Xna::Framework::Graphics {

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
        : renderer(graphicsDevice.GetRendererInternal()) {
    }

    SpriteBatch::SpriteBatch()
        : renderer(nullptr) {
    }

    SpriteBatch::~SpriteBatch() = default;

    void SpriteBatch::Begin()
    {
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
        (void)sprite_sort_mode;
        (void)blend_state;
    }

    void SpriteBatch::End()
    {
    }

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        SDL_Texture* nativeTexture = texture.GetNativeTextureInternal();
        if (!renderer || !nativeTexture) {
            return;
        }

        Rectangle bounds = texture.getBoundsProperty();
        float width = static_cast<float>(bounds.Width);
        float height = static_cast<float>(bounds.Height);

        if (width <= 0.0f) {
            width = 100.0f;
        }
        if (height <= 0.0f) {
            height = 100.0f;
        }

        SDL_FRect dstRect = {x, y, width, height};
        SDL_RenderTexture(renderer, nativeTexture, nullptr, &dstRect);
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>::value_type& value,
                           const Rectangle& x,
                           const Rectangle& y,
                           Color color)
    {
        (void)value;
        (void)x;
        (void)y;
        (void)color;
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>& value,
                           const Rectangle& x,
                           const Rectangle& y,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float x1)
    {
        (void)value;
        (void)x;
        (void)y;
        (void)color;
        (void)rotation_rad;
        (void)origin;
        (void)effect;
        (void)x1;
    }
}