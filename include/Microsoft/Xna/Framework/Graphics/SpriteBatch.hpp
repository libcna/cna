#pragma once

#include <optional>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

struct SDL_Renderer;

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice;
    class ISpriteBatchBackend;

    /**
     * @brief Provides simple batched sprite drawing.
     *
     * This class uses a backend abstraction to handle the actual rendering,
     * such as SDL_Renderer or EasyGL.
     */
    class SpriteBatch {
    private:
        std::unique_ptr<ISpriteBatchBackend> backend_;
        bool begun = false;

    public:
        /**
         * @brief Creates a sprite batch bound to a graphics device.
         *
         * @param graphicsDevice Graphics device used for rendering.
         */
        explicit SpriteBatch(GraphicsDevice& graphicsDevice);

        /**
         * @brief Creates an empty sprite batch.
         */
        SpriteBatch();

        /**
         * @brief Destroys the sprite batch.
         */
        ~SpriteBatch();

        /**
         * @brief Begins a sprite batch with default settings.
         */
        void Begin();

        /**
         * @brief Begins a sprite batch with explicit settings.
         *
         * @param sprite_sort_mode Sprite sort mode.
         * @param blend_state Blend state.
         */
        void Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state);

        /**
         * @brief Ends the current sprite batch.
         */
        void End();

        /**
         * @brief Draws a texture at the given position.
         *
         * @param texture Texture to draw.
         * @param x X coordinate.
         * @param y Y coordinate.
         */
        void Draw(const Texture2D& texture, float x, float y);

        void Draw(const std::optional<Texture2D>::value_type& value,
                  const Rectangle& sourceRectangle,
                  const Rectangle& destinationRectangle,
                  Color color);

        void Draw(const std::optional<Texture2D>& value,
                  const Rectangle& sourceRectangle,
                  const Rectangle& destinationRectangle,
                  Color color,
                  float rotation_rad,
                  Vector2 origin,
                  SpriteEffects effect,
                  float layerDepth);
    };
}