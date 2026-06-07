#pragma once

#include <optional>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

struct SDL_Renderer;

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class SpriteFont;
}

namespace CNA::Internal::Backends
{
    class ISpriteBatchBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;

    /// High-performance batched sprite rendering engine matching XNA 4.0 SpriteBatch.
    class SpriteBatch
    {
    private:
        struct SpriteInfo {
            const Texture2D* texture = nullptr;
            Rectangle destRect       = {0, 0, 0, 0};
            Rectangle srcRect        = {0, 0, 0, 0};
            Color color              = Color(255, 255, 255, 255);
            float rotation           = 0.0f;
            Vector2 origin           = Vector2(0.0f, 0.0f);
            SpriteEffects effects    = SpriteEffects::None;
            float layerDepth         = 0.0f;
        };

        std::unique_ptr<ISpriteBatchBackend> backend_;
        GraphicsDevice* graphicsDevice_ = nullptr;
        bool begun = false;
        SpriteSortMode sortMode_ = SpriteSortMode::Deferred;
        std::vector<SpriteInfo> spriteQueue_;

        void pushSprite(const Texture2D& texture,
                        const Rectangle& dest, const Rectangle& src,
                        Color color, float rotation, Vector2 origin,
                        SpriteEffects effects, float layerDepth);
        void flushBatch();
        void flushSingle(const SpriteInfo& s);

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

        /**
         * @brief Draws a string of text using the given font.
         *
         * @param spriteFont Font providing the glyph atlas and layout.
         * @param text       Text to render.
         * @param position   Top-left position, in pixels.
         * @param color      Tint color.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const std::string& text,
                        Vector2 position,
                        Color color);

        /// Draws text with rotation, origin, uniform scale, flipping and layer depth.
        void DrawString(const SpriteFont& spriteFont,
                        const std::string& text,
                        Vector2 position,
                        Color color,
                        float rotation,
                        Vector2 origin,
                        float scale,
                        SpriteEffects effects,
                        float layerDepth);

        /// Draws text with rotation, origin, non-uniform scale, flipping and layer depth.
        void DrawString(const SpriteFont& spriteFont,
                        const std::string& text,
                        Vector2 position,
                        Color color,
                        float rotation,
                        Vector2 origin,
                        Vector2 scale,
                        SpriteEffects effects,
                        float layerDepth);
    };
}
