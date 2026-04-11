#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include <SDL3_image/SDL_image.h>

namespace Microsoft::Xna::Framework::Graphics {

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
    {
        renderer = graphicsDevice.GetRenderer();
    }

    SpriteBatch::SpriteBatch()
        : renderer(nullptr)
    {
    }

    SpriteBatch::~SpriteBatch()
    {
    }

    void SpriteBatch::Begin()
    {
        // Optional future setup before rendering.
    }

    void SpriteBatch::End()
    {
        // Do not call SDL_RenderPresent here.
        // Presentation must happen exactly once per frame,
        // typically through GraphicsDevice::Present().
    }

    void SpriteBatch::Draw(SDL_Texture* texture, float x, float y)
    {
        SDL_FRect dstFRect = {
            static_cast<float>(x),
            static_cast<float>(y),
            100.0f,
            100.0f
        };
        SDL_RenderTexture(renderer, texture, nullptr, &dstFRect);
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>::value_type& value,
                           const Rectangle& x,
                           const Rectangle& y,
                           Color color)
    {
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
    }
}