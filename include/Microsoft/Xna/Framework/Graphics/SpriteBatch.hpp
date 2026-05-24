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
}

namespace CNA::Internal::Backends {
    class ISpriteBatchBackend;
}

namespace Microsoft::Xna::Framework::Graphics {
    using namespace CNA::Internal::Backends;

    /**
     * @brief High-performance batched sprite rendering engine.
     *
     * SpriteBatch is not "just a drawing function" — it's a sophisticated batching engine that:
     * - Collects draw-call requests from the application
     * - Sorts sprites according to the specified SpriteSortMode
     * - Generates optimized vertex buffers
     * - Minimizes texture switches
     * - Minimizes shader switches
     * - Minimizes blend state changes
     * - Submits large batches to the GPU for efficient rendering
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
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  Color color);

        void Draw(const std::optional<Texture2D>& value,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  Color color,
                  float rotation_rad,
                  Vector2 origin,
                  SpriteEffects effect,
                  float layerDepth);
    };
}