#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.h"
#include <SDL3_image/SDL_image.h>

namespace Microsoft::Xna::Framework::Graphics {

    SpriteBatch::SpriteBatch(GraphicsDevice graphicsDevice) {
        renderer = graphicsDevice.GetRenderer();
    }

    SpriteBatch::SpriteBatch() {
    }

    SpriteBatch::~SpriteBatch() {}

    void SpriteBatch::Begin() {
        // Setting before rendering, for example blend mode
    }

    void SpriteBatch::End() {
        // Clean up after renderring
        SDL_RenderPresent(renderer);
    }

    void SpriteBatch::Draw(SDL_Texture* texture, float x, float y) {
        SDL_FRect dstFRect = { static_cast<float>(x), static_cast<float>(y), 100.0f, 100.0f };  // 100x100 is the size of the image
        SDL_RenderTexture(renderer, texture, nullptr, &dstFRect);
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state) {
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>::value_type &value, const Rectangle &x, const Rectangle &y,
        Color color) {
    }

    void SpriteBatch::Draw(const std::optional<Texture2D> &value, const Rectangle &x, const Rectangle &y, Color color,
        float rotation_rad, Vector2 origin, SpriteEffects effect, float x1) {
    }
}
