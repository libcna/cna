// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "System/Text/StringBuilder.hpp"

struct SDL_Renderer;

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
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

    /** @brief High-performance batched sprite rendering engine matching XNA 4.0 SpriteBatch. */
    class SpriteBatch : public GraphicsResource
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
        bool begun = false;
        SpriteSortMode sortMode_    = SpriteSortMode::Deferred;
        Matrix transformMatrix_     = Matrix::getIdentityProperty();
        Effect* customEffect_       = nullptr;
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

        /** @brief Creates an empty sprite batch. */
        NOXNA SpriteBatch();

        /**
         * @brief NOXNA test-only: creates a sprite batch bound directly to an explicit backend,
         *        bypassing GraphicsDevice entirely. Enables deterministic unit testing of
         *        Begin/Draw/End batching and sort-mode logic against a mock/recording backend
         *        without a real graphics context.
         *
         * @param backend Backend implementation to receive Begin/End/Draw calls.
         */
        NOXNA explicit SpriteBatch(std::unique_ptr<ISpriteBatchBackend> backend);

        /** @brief Destructor. */
        NOXNA ~SpriteBatch() override;

        /** @brief Returns the fully-qualified .NET type name of this object. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Begins a sprite batch with default settings (AlphaBlend, LinearClamp, no depth). */
        void Begin();

        /**
         * @brief Begins a sprite batch with explicit sort mode and blend state.
         *
         * @param sprite_sort_mode Sprite sort mode.
         * @param blend_state Blend state.
         */
        void Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state);

        /**
         * @brief Begins a sprite batch with state objects; transform defaults to Identity.
         *
         * @param sortMode         Sprite sort mode.
         * @param blendState       Blend state.
         * @param samplerState     Sampler state, or nullptr to use LinearClamp.
         * @param depthStencilState Depth-stencil state, or nullptr to use None.
         * @param rasterizerState  Rasterizer state, or nullptr to use CullCounterClockwise.
         */
        void Begin(SpriteSortMode sortMode,
                   BlendState blendState,
                   SamplerState* samplerState,
                   DepthStencilState* depthStencilState,
                   RasterizerState* rasterizerState);

        /**
         * @brief Begins a sprite batch with a custom effect; transform defaults to Identity.
         *
         * @param sortMode         Sprite sort mode.
         * @param blendState       Blend state.
         * @param samplerState     Sampler state, or nullptr to use LinearClamp.
         * @param depthStencilState Depth-stencil state, or nullptr to use None.
         * @param rasterizerState  Rasterizer state, or nullptr to use CullCounterClockwise.
         * @param effect           Custom effect to apply, or nullptr to use the default sprite effect.
         */
        void Begin(SpriteSortMode sortMode,
                   BlendState blendState,
                   SamplerState* samplerState,
                   DepthStencilState* depthStencilState,
                   RasterizerState* rasterizerState,
                   Effect* effect);

        /**
         * @brief Begins a sprite batch with all XNA parameters including a transform matrix.
         *
         * @param sortMode         Sprite sort mode.
         * @param blendState       Blend state.
         * @param samplerState     Sampler state, or nullptr to use LinearClamp.
         * @param depthStencilState Depth-stencil state, or nullptr to use None.
         * @param rasterizerState  Rasterizer state, or nullptr to use CullCounterClockwise.
         * @param effect           Custom effect, or nullptr for the default sprite effect.
         * @param transformMatrix  Matrix applied to all sprites before projection.
         */
        void Begin(SpriteSortMode sortMode,
                   BlendState blendState,
                   SamplerState* samplerState,
                   DepthStencilState* depthStencilState,
                   RasterizerState* rasterizerState,
                   Effect* effect,
                   Matrix transformMatrix);

        /** @brief Flushes the current batch and ends the sprite drawing session. */
        void End();

        /**
         * @brief Draws a texture at the given screen-space position.
         *
         * @param texture Texture to draw.
         * @param x X coordinate in pixels.
         * @param y Y coordinate in pixels.
         * @throws System::ArgumentOutOfRangeException if @p x or @p y is non-finite or cannot
         *         be represented by SpriteBatch's Int32 destination rectangle.
         */
        NOXNA void Draw(const Texture2D& texture, float x, float y);

        /**
         * @brief Draws a region of a texture into a destination rectangle with a tint color.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Rectangle that defines the draw destination in screen space.
         * @param sourceRectangle      Rectangle that selects the region of the texture to draw.
         * @param color                Tint color; use Color::White for no tint.
         */
        NOXNA void Draw(const Texture2D& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  Color color);

        /**
         * @brief Draws a region of a texture into a destination rectangle with rotation, origin, effects, and depth.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Rectangle that defines the draw destination in screen space.
         * @param sourceRectangle      Rectangle that selects the region of the texture to draw.
         * @param color                Tint color.
         * @param rotation_rad         Rotation angle in radians.
         * @param origin               Point within the texture used as the rotation origin.
         * @param effect               Sprite flipping flags.
         * @param layerDepth           Depth value for sort ordering (0 = front, 1 = back).
         */
        NOXNA void Draw(const Texture2D& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  Color color,
                  float rotation_rad,
                  Vector2 origin,
                  SpriteEffects effect,
                  float layerDepth);

        /**
         * @brief Draws a texture at the given position with a tint color.
         *
         * @param texture  Texture to draw.
         * @param position Position in screen space.
         * @param color    Tint color.
         * @throws System::ArgumentOutOfRangeException if @p position is non-finite or cannot
         *         be represented by SpriteBatch's Int32 destination rectangle.
         */
        void Draw(const Texture2D& texture, Vector2 position, Color color);
        /**
         * @brief Draws an optional source region of a texture at a position.
         *
         * @param texture         Texture to draw.
         * @param position        Position in screen space.
         * @param sourceRectangle Optional source rectangle; draws the whole texture if empty.
         * @param color           Tint color.
         * @throws System::ArgumentOutOfRangeException if @p position is non-finite or cannot
         *         be represented by SpriteBatch's Int32 destination rectangle.
         */
        void Draw(const Texture2D& texture, Vector2 position,
                  std::optional<Rectangle> sourceRectangle, Color color);
        /**
         * @brief Draws a texture at a position with rotation, origin, uniform scale, effects, and depth.
         *
         * @param texture         Texture to draw.
         * @param position        Position in screen space.
         * @param sourceRectangle Optional source rectangle.
         * @param color           Tint color.
         * @param rotation        Rotation angle in radians.
         * @param origin          Rotation/scale origin in texture space.
         * @param scale           Uniform scale factor.
         * @param effects         Sprite flipping flags.
         * @param layerDepth      Depth value for sort ordering.
         * @throws System::ArgumentOutOfRangeException if @p position or @p scale is non-finite,
         *         or if the calculated Int32 destination rectangle is out of range.
         */
        void Draw(const Texture2D& texture, Vector2 position,
                  std::optional<Rectangle> sourceRectangle, Color color,
                  float rotation, Vector2 origin, float scale,
                  SpriteEffects effects, float layerDepth);
        /**
         * @brief Draws a texture at a position with rotation, origin, non-uniform scale, effects, and depth.
         *
         * @param texture         Texture to draw.
         * @param position        Position in screen space.
         * @param sourceRectangle Optional source rectangle.
         * @param color           Tint color.
         * @param rotation        Rotation angle in radians.
         * @param origin          Rotation/scale origin in texture space.
         * @param scale           Non-uniform scale vector.
         * @param effects         Sprite flipping flags.
         * @param layerDepth      Depth value for sort ordering.
         * @throws System::ArgumentOutOfRangeException if @p position or @p scale is non-finite,
         *         or if the calculated Int32 destination rectangle is out of range.
         */
        void Draw(const Texture2D& texture, Vector2 position,
                  std::optional<Rectangle> sourceRectangle, Color color,
                  float rotation, Vector2 origin, Vector2 scale,
                  SpriteEffects effects, float layerDepth);
        /**
         * @brief Draws a texture scaled to fill a destination rectangle.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Draw destination in screen space.
         * @param color                Tint color.
         */
        void Draw(const Texture2D& texture,
                  const Rectangle& destinationRectangle, Color color);
        /**
         * @brief Draws an optional source region of a texture into a destination rectangle.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Draw destination in screen space.
         * @param sourceRectangle      Optional source rectangle.
         * @param color                Tint color.
         */
        void Draw(const Texture2D& texture,
                  const Rectangle& destinationRectangle,
                  std::optional<Rectangle> sourceRectangle, Color color);
        /**
         * @brief Draws an optional source region of a texture into a destination rectangle with
         *        rotation, origin, effects, and depth.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Draw destination in screen space.
         * @param sourceRectangle      Optional source rectangle; draws the whole texture if empty.
         * @param color                Tint color.
         * @param rotation_rad         Rotation angle in radians.
         * @param origin               Point within the texture used as the rotation origin.
         * @param effect               Sprite flipping flags.
         * @param layerDepth           Depth value for sort ordering (0 = front, 1 = back).
         */
        void Draw(const Texture2D& texture,
                  const Rectangle& destinationRectangle,
                  std::optional<Rectangle> sourceRectangle,
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
         * @throws std::invalid_argument if @p text contains a character @p spriteFont cannot
         *         render and no defaultCharacter is set.
         * @throws System::ArgumentOutOfRangeException if @p position is non-finite or the
         *         calculated Int32 glyph destination is out of range.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const std::string& text,
                        Vector2 position,
                        Color color);

        /**
         * @brief Draws text with rotation, origin, uniform scale, flipping, and layer depth.
         *
         * @param spriteFont Font providing the glyph atlas and layout.
         * @param text       Text to render.
         * @param position   Position in screen space.
         * @param color      Tint color.
         * @param rotation   Rotation angle in radians.
         * @param origin     Rotation/scale origin in screen space.
         * @param scale      Uniform scale factor.
         * @param effects    Sprite flipping flags.
         * @param layerDepth Depth value for sort ordering.
         * @throws std::invalid_argument if @p text contains a character @p spriteFont cannot
         *         render and no defaultCharacter is set.
         * @throws System::ArgumentOutOfRangeException if @p position, @p rotation, @p origin, or
         *         @p scale is non-finite, or if a calculated Int32 glyph destination is out of
         *         range.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const std::string& text,
                        Vector2 position,
                        Color color,
                        float rotation,
                        Vector2 origin,
                        float scale,
                        SpriteEffects effects,
                        float layerDepth);

        /**
         * @brief Draws text with rotation, origin, non-uniform scale, flipping, and layer depth.
         *
         * @param spriteFont Font providing the glyph atlas and layout.
         * @param text       Text to render.
         * @param position   Position in screen space.
         * @param color      Tint color.
         * @param rotation   Rotation angle in radians.
         * @param origin     Rotation/scale origin in screen space.
         * @param scale      Non-uniform scale vector.
         * @param effects    Sprite flipping flags.
         * @param layerDepth Depth value for sort ordering.
         * @throws std::invalid_argument if @p text contains a character @p spriteFont cannot
         *         render and no defaultCharacter is set.
         * @throws System::ArgumentOutOfRangeException if @p position, @p rotation, @p origin, or
         *         @p scale is non-finite, or if a calculated Int32 glyph destination is out of
         *         range.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const std::string& text,
                        Vector2 position,
                        Color color,
                        float rotation,
                        Vector2 origin,
                        Vector2 scale,
                        SpriteEffects effects,
                        float layerDepth);

        /**
         * @brief Draws a StringBuilder as text using the given font.
         *
         * @param spriteFont Font providing the glyph atlas and layout.
         * @param text       Text to render.
         * @param position   Top-left position, in pixels.
         * @param color      Tint color.
         * @throws System::ArgumentOutOfRangeException if @p position is non-finite or the
         *         calculated Int32 glyph destination is out of range.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const System::Text::StringBuilder& text,
                        Vector2 position,
                        Color color);
        /**
         * @brief Draws a StringBuilder as text with rotation, origin, uniform scale, flipping, and depth.
         *
         * @param spriteFont Font providing the glyph atlas and layout.
         * @param text       Text to render.
         * @param position   Position in screen space.
         * @param color      Tint color.
         * @param rotation   Rotation angle in radians.
         * @param origin     Rotation/scale origin in screen space.
         * @param scale      Uniform scale factor.
         * @param effects    Sprite flipping flags.
         * @param layerDepth Depth value for sort ordering.
         * @throws System::ArgumentOutOfRangeException if @p position, @p rotation, @p origin, or
         *         @p scale is non-finite, or if a calculated Int32 glyph destination is out of
         *         range.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const System::Text::StringBuilder& text,
                        Vector2 position,
                        Color color,
                        float rotation,
                        Vector2 origin,
                        float scale,
                        SpriteEffects effects,
                        float layerDepth);
        /**
         * @brief Draws a StringBuilder as text with rotation, origin, non-uniform scale, flipping, and depth.
         *
         * @param spriteFont Font providing the glyph atlas and layout.
         * @param text       Text to render.
         * @param position   Position in screen space.
         * @param color      Tint color.
         * @param rotation   Rotation angle in radians.
         * @param origin     Rotation/scale origin in screen space.
         * @param scale      Non-uniform scale vector.
         * @param effects    Sprite flipping flags.
         * @param layerDepth Depth value for sort ordering.
         * @throws System::ArgumentOutOfRangeException if @p position, @p rotation, @p origin, or
         *         @p scale is non-finite, or if a calculated Int32 glyph destination is out of
         *         range.
         */
        void DrawString(const SpriteFont& spriteFont,
                        const System::Text::StringBuilder& text,
                        Vector2 position,
                        Color color,
                        float rotation,
                        Vector2 origin,
                        Vector2 scale,
                        SpriteEffects effects,
                        float layerDepth);
    };
}
