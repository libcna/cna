#include "CNA/SpriteBatch.h"
#include <SDL3_image/SDL_image.h>

namespace CNA {

    SpriteBatch::SpriteBatch(GraphicsDevice* graphicsDevice) {
        renderer = graphicsDevice->GetRenderer();
    }

    SpriteBatch::~SpriteBatch() {}

    void SpriteBatch::Begin() {
        // Setting before rendering, for example blend mode
    }

    void SpriteBatch::End() {
        // Clean up after renderring
    }

    void SpriteBatch::Draw(SDL_Texture* texture, float x, float y) {
        SDL_FRect dstFRect = { static_cast<float>(x), static_cast<float>(y), 100.0f, 100.0f };  // 100x100 is the size of the image
        SDL_RenderTexture(renderer, texture, nullptr, &dstFRect);
    }
}
