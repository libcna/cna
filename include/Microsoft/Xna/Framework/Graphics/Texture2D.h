#ifndef CNA_TEXTURE2D_H
#define CNA_TEXTURE2D_H

#include <SDL3/SDL.h>

namespace Microsoft::Xna::Framework::Graphics {

    class Texture2D {
    public:
        Texture2D(SDL_Renderer* renderer, const char* filePath);
        ~Texture2D();

        SDL_Texture* GetTexture() const { return texture; }

    private:
        SDL_Texture* texture;
    };
}

#endif // CNA_TEXTURE2D_H
