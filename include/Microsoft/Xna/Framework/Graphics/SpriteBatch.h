#ifndef CNA_SPRITEBATCH_H
#define CNA_SPRITEBATCH_H

#include <optional>

#include "GraphicsDevice.h"
#include <SDL3/SDL.h>

#include "BlendState.h"
#include "SpriteEffects.h"
#include "SpriteSortMode.h"
#include "Texture2D.h"
#include "Microsoft/Xna/Framework/Rectangle.h"
#include "Microsoft/Xna/Framework/Vector2.h"

namespace Microsoft::Xna::Framework::Graphics {

    class SpriteBatch {
    public:
        SpriteBatch(GraphicsDevice& graphicsDevice);

        SpriteBatch();

        ~SpriteBatch();

        void Begin();
        void End();
        void Draw(SDL_Texture* texture, float x, float y);

        void Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state);

        void Draw(const std::optional<Texture2D>::value_type & value, const Rectangle & x, const Rectangle & y, Color color);

        void Draw(const std::optional<Texture2D> & value, const Rectangle & x, const Rectangle & y, Color color, float rotation_rad, Vector2 origin,
                 SpriteEffects effect,
                 float x1);

    private:
        SDL_Renderer* renderer;
    };
}

#endif // CNA_SPRITEBATCH_H
