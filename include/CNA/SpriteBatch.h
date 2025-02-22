#ifndef CNA_SPRITEBATCH_H
#define CNA_SPRITEBATCH_H

#include "GraphicsDevice.h"
#include <SDL3/SDL.h>

namespace CNA {

    class SpriteBatch {
    public:
        SpriteBatch(GraphicsDevice* graphicsDevice);
        ~SpriteBatch();

        void Begin();
        void End();
        void Draw(SDL_Texture* texture, float x, float y);

    private:
        SDL_Renderer* renderer;
    };
}

#endif // CNA_SPRITEBATCH_H
