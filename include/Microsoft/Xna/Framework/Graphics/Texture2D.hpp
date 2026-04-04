#pragma once

#include <string>
#include <SDL3/SDL.h>

#include "Microsoft/Xna/Framework/Rectangle.hpp"


namespace Microsoft::Xna::Framework::Graphics {

    class Texture2D {
    public:
        Texture2D(SDL_Renderer* renderer, const char* filePath);

        Texture2D();
        Texture2D(const std::string &assetName);


        ~Texture2D();

        SDL_Texture* GetTexture() const { return texture; }
        [[nodiscard]] Rectangle getBoundsProperty() const;
    private:
        int width;
        int height;


    private:
        SDL_Texture* texture;
    };
}
