#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3_image/SDL_image.h>

#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class Texture2D::Impl {
    public:
        SDL_Texture* texture = nullptr;
    };

    Rectangle Texture2D::getBoundsProperty() const
    {
        return {0, 0, width, height};
    }

    Texture2D::Texture2D()
        : impl_(std::make_shared<Impl>()) {
    }

    Texture2D::Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)
        : impl_(std::make_shared<Impl>())
    {
        if (assetName.empty()) {
            return;
        }

        SDL_Renderer* renderer = graphicsDevice.GetRendererInternal();
        if (!renderer) {
            throw std::runtime_error("Texture2D load failed: GraphicsDevice has no renderer.");
        }

        impl_->texture = IMG_LoadTexture(renderer, assetName.c_str());
        if (!impl_->texture) {
            throw std::runtime_error(
                "Failed to load texture: " + assetName + " Error: " + SDL_GetError()
            );
        }

        float w = 0.0f;
        float h = 0.0f;
        if (SDL_GetTextureSize(impl_->texture, &w, &h)) {
            width = static_cast<int>(w);
            height = static_cast<int>(h);
        }
    }

    Texture2D::~Texture2D()
    {
        if (impl_ && impl_->texture) {
            SDL_DestroyTexture(impl_->texture);
            impl_->texture = nullptr;
        }
    }

    SDL_Texture* Texture2D::GetNativeTextureInternal() const
    {
        return impl_ ? impl_->texture : nullptr;
    }
}
