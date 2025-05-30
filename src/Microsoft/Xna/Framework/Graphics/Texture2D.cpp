#include "Microsoft/Xna/Framework/Graphics/Texture2D.h"
#include <SDL3_image/SDL_image.h>
#include <iostream>

namespace Microsoft::Xna::Framework::Graphics {

    Texture2D::Texture2D(SDL_Renderer* renderer, const char* filePath):
    Bounds( [*this]() { return Rectangle(0, 0, width, height); })
{
        texture = IMG_LoadTexture(renderer, filePath);
        if (!texture) {
            std::cerr << "Failed to load texture: " << SDL_GetError() << std::endl;
        }
    }

    Texture2D::~Texture2D()
    {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
}
