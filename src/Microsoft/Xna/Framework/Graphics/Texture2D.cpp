#include "Microsoft/Xna/Framework/Graphics/Texture2D.h"
#include <SDL3_image/SDL_image.h>
#include <iostream>

#include "CNA/Prop.h"

namespace Microsoft::Xna::Framework::Graphics {

    Rectangle Texture2D::BoundsProperty() const { return {0, 0, width, height}; }
    Texture2D::Texture2D(SDL_Renderer* renderer, const char* filePath)
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
