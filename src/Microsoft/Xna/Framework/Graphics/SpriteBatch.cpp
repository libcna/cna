#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    namespace {
        SDL_FlipMode ToSdlFlip(const SpriteEffects effect)
        {
            const bool flipH =
                (static_cast<int>(effect) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
            const bool flipV =
                (static_cast<int>(effect) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;

            if (flipH && flipV) {
                return SDL_FLIP_HORIZONTAL_AND_VERTICAL;
            }
            if (flipH) {
                return SDL_FLIP_HORIZONTAL;
            }
            if (flipV) {
                return SDL_FLIP_VERTICAL;
            }
            return SDL_FLIP_NONE;
        }
    }

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
        : renderer(graphicsDevice.GetRendererInternal()),
          begun(false)
    {
    }

    SpriteBatch::SpriteBatch()
        : renderer(nullptr),
          begun(false)
    {
    }

    SpriteBatch::~SpriteBatch() = default;

    void SpriteBatch::Begin()
    {
        if (!renderer) {
            throw std::runtime_error("SpriteBatch::Begin failed: renderer is null.");
        }
        begun = true;
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
        (void)sprite_sort_mode;
        (void)blend_state;
        Begin();
    }

    void SpriteBatch::End()
    {
        if (!begun) {
            return;
        }
        begun = false;
    }

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        if (!begun) {
            throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        }

        SDL_Texture* nativeTexture = texture.GetNativeTextureInternal();
        if (!nativeTexture) {
            return;
        }

        const Rectangle bounds = texture.getBoundsProperty();
        SDL_FRect dst {
            x,
            y,
            static_cast<float>(bounds.Width),
            static_cast<float>(bounds.Height)
        };

        if (!SDL_RenderTexture(renderer, nativeTexture, nullptr, &dst)) {
            throw std::runtime_error(
                std::string("SDL_RenderTexture failed: ") + SDL_GetError()
            );
        }
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>::value_type& value,
                           const Rectangle& sourceRectangle,
                           const Rectangle& destinationRectangle,
                           Color color)
    {
        if (!begun) {
            throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        }

        SDL_Texture* nativeTexture = value.GetNativeTextureInternal();
        if (!nativeTexture) {
            return;
        }

        SDL_SetTextureColorMod(
            nativeTexture,
            color.getRProperty(),
            color.getGProperty(),
            color.getBProperty()
        );
        SDL_SetTextureAlphaMod(nativeTexture, color.getAProperty());

        const SDL_FRect src {
            static_cast<float>(sourceRectangle.X),
            static_cast<float>(sourceRectangle.Y),
            static_cast<float>(sourceRectangle.Width),
            static_cast<float>(sourceRectangle.Height)
        };

        const SDL_FRect dst {
            static_cast<float>(destinationRectangle.X),
            static_cast<float>(destinationRectangle.Y),
            static_cast<float>(destinationRectangle.Width),
            static_cast<float>(destinationRectangle.Height)
        };

        if (!SDL_RenderTexture(renderer, nativeTexture, &src, &dst)) {
            throw std::runtime_error(
                std::string("SDL_RenderTexture failed: ") + SDL_GetError()
            );
        }
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>& value,
                           const Rectangle& sourceRectangle,
                           const Rectangle& destinationRectangle,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float layerDepth)
    {
        (void)layerDepth;

        if (!begun) {
            throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        }

        if (!value.has_value()) {
            return;
        }

        SDL_Texture* nativeTexture = value->GetNativeTextureInternal();
        if (!nativeTexture) {
            return;
        }

        SDL_SetTextureColorMod(
            nativeTexture,
            color.getRProperty(),
            color.getGProperty(),
            color.getBProperty()
        );
        SDL_SetTextureAlphaMod(nativeTexture, color.getAProperty());

        const SDL_FRect src {
            static_cast<float>(sourceRectangle.X),
            static_cast<float>(sourceRectangle.Y),
            static_cast<float>(sourceRectangle.Width),
            static_cast<float>(sourceRectangle.Height)
        };

        const SDL_FRect dst {
            static_cast<float>(destinationRectangle.X),
            static_cast<float>(destinationRectangle.Y),
            static_cast<float>(destinationRectangle.Width),
            static_cast<float>(destinationRectangle.Height)
        };

        const SDL_FPoint center {
            origin.X,
            origin.Y
        };

        const double rotationDeg = static_cast<double>(rotation_rad) * 180.0 / 3.14159265358979323846;
        const SDL_FlipMode flipMode = ToSdlFlip(effect);

        if (!SDL_RenderTextureRotated(renderer, nativeTexture, &src, &dst, rotationDeg, &center, flipMode)) {
            throw std::runtime_error(
                std::string("SDL_RenderTextureRotated failed: ") + SDL_GetError()
            );
        }
    }
}