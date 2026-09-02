// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
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

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
    class GraphicsDevice;
    class SpriteFont;
}

namespace CNA::Internal::Renderers
{
    class ISpriteBatchRenderer;
}

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Renderers;

    /** @brief High-performance batched sprite rendering engine matching XNA 4.0 SpriteBatch. */
    class SpriteBatch : public GraphicsResource
    {
    private:
        struct SpriteInfo {
            std::shared_ptr<ITextureRenderer> texture;
            // Destination kept as XNA/FNA keep it: unrounded, so a sprite drawn at a fractional
            // position stays between pixels all the way to the renderer.
            float destX              = 0.0f;
            float destY              = 0.0f;
            float destWidth          = 0.0f;
            float destHeight         = 0.0f;
            Rectangle srcRect        = {0, 0, 0, 0};
            Color color              = Color(255, 255, 255, 255);
            float rotation           = 0.0f;
            Vector2 origin           = Vector2(0.0f, 0.0f);
            SpriteEffects effects    = SpriteEffects::None;
            float layerDepth         = 0.0f;
        };

        std::unique_ptr<ISpriteBatchRenderer> renderer_;
        bool begun = false;
        SpriteSortMode sortMode_    = SpriteSortMode::Deferred;
        Matrix transformMatrix_     = Matrix::getIdentityProperty();
        Effect* customEffect_       = nullptr;
        std::vector<SpriteInfo> spriteQueue_;

        void pushSprite(const Texture2D& texture,
                        float destX, float destY, float destWidth, float destHeight,
                        const Rectangle& src,
                        Color color, float rotation, Vector2 origin,
                        SpriteEffects effects, float layerDepth);
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
        CNAEXT SpriteBatch();

        /**
         * @brief CNAEXT test-only: creates a sprite batch bound directly to an explicit renderer,
         *        bypassing GraphicsDevice entirely. Enables deterministic unit testing of
         *        Begin/Draw/End batching and sort-mode logic against a mock/recording renderer
         *        without a real graphics context.
         *
         * @param renderer Renderer implementation to receive Begin/End/Draw calls.
         */
        CNAEXT explicit SpriteBatch(std::unique_ptr<ISpriteBatchRenderer> renderer);

        /** @brief Destructor. */
        CNAEXT ~SpriteBatch() override;

        /** @brief Returns the fully-qualified .NET type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

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
                   const SamplerState* samplerState,
                   const DepthStencilState* depthStencilState,
                   const RasterizerState* rasterizerState);

        /**
         * @brief Begins a sprite batch with nullable XNA state objects; transform defaults to
         *        Identity.
         *
         * @param sortMode Sprite sort mode.
         * @param blendState Blend state, or nullptr to use AlphaBlend.
         * @param samplerState Sampler state, or nullptr to use LinearClamp.
         * @param depthStencilState Depth-stencil state, or nullptr to use None.
         * @param rasterizerState Rasterizer state, or nullptr to use CullCounterClockwise.
         */
        void Begin(SpriteSortMode sortMode,
                   const BlendState* blendState,
                   const SamplerState* samplerState,
                   const DepthStencilState* depthStencilState,
                   const RasterizerState* rasterizerState);

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
                   const SamplerState* samplerState,
                   const DepthStencilState* depthStencilState,
                   const RasterizerState* rasterizerState,
                   Effect* effect);

        /**
         * @brief Begins a sprite batch with nullable XNA state objects and a custom effect;
         *        transform defaults to Identity.
         *
         * @param sortMode Sprite sort mode.
         * @param blendState Blend state, or nullptr to use AlphaBlend.
         * @param samplerState Sampler state, or nullptr to use LinearClamp.
         * @param depthStencilState Depth-stencil state, or nullptr to use None.
         * @param rasterizerState Rasterizer state, or nullptr to use CullCounterClockwise.
         * @param effect Custom effect to apply, or nullptr to use the default sprite effect.
         */
        void Begin(SpriteSortMode sortMode,
                   const BlendState* blendState,
                   const SamplerState* samplerState,
                   const DepthStencilState* depthStencilState,
                   const RasterizerState* rasterizerState,
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
                   const SamplerState* samplerState,
                   const DepthStencilState* depthStencilState,
                   const RasterizerState* rasterizerState,
                   Effect* effect,
                   Matrix transformMatrix);

        /**
         * @brief Begins a sprite batch with all XNA parameters and nullable state objects.
         *
         * @param sortMode Sprite sort mode.
         * @param blendState Blend state, or nullptr to use AlphaBlend.
         * @param samplerState Sampler state, or nullptr to use LinearClamp.
         * @param depthStencilState Depth-stencil state, or nullptr to use None.
         * @param rasterizerState Rasterizer state, or nullptr to use CullCounterClockwise.
         * @param effect Custom effect, or nullptr for the default sprite effect.
         * @param transformMatrix Matrix applied to all sprites before projection.
         */
        void Begin(SpriteSortMode sortMode,
                   const BlendState* blendState,
                   const SamplerState* samplerState,
                   const DepthStencilState* depthStencilState,
                   const RasterizerState* rasterizerState,
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
         * @throws System::ArgumentOutOfRangeException if @p x or @p y cannot be represented by
         *         SpriteBatch's Int32 destination rectangle. Non-finite values are accepted and
         *         carried into the vertex path, as XNA does.
         */
        CNAEXT void Draw(const Texture2D& texture, float x, float y);

        /**
         * @brief Draws a region of a texture into a destination rectangle with a tint color.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Rectangle that defines the draw destination in screen space.
         * @param sourceRectangle      Rectangle that selects the region of the texture to draw.
         * @param color                Tint color; use Color::White for no tint.
         */
        CNAEXT void Draw(const Texture2D& texture,
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
        CNAEXT void Draw(const Texture2D& texture,
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
         * @throws System::ArgumentOutOfRangeException if @p position cannot be represented by
         *         SpriteBatch's Int32 destination rectangle. Non-finite values are accepted and
         *         carried into the vertex path, as XNA does.
         */
        void Draw(const Texture2D& texture, Vector2 position, Color color);
        /**
         * @brief Draws an optional source region of a texture at a position.
         *
         * @param texture         Texture to draw.
         * @param position        Position in screen space.
         * @param sourceRectangle Optional source rectangle; draws the whole texture if empty.
         * @param color           Tint color.
         * @throws System::ArgumentOutOfRangeException if @p position cannot be represented by
         *         SpriteBatch's Int32 destination rectangle. Non-finite values are accepted and
         *         carried into the vertex path, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if the calculated Int32 destination
         *         rectangle is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if the calculated Int32 destination
         *         rectangle is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if the calculated Int32 glyph destination
         *         is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if a calculated Int32 glyph destination
         *         is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if a calculated Int32 glyph destination
         *         is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if the calculated Int32 glyph destination
         *         is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if a calculated Int32 glyph destination
         *         is out of range. Non-finite values are accepted, as XNA does.
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
         * @throws System::ArgumentOutOfRangeException if a calculated Int32 glyph destination
         *         is out of range. Non-finite values are accepted, as XNA does.
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

        /**
         * @brief Draws a triangle-list 2D mesh through @p effect's own bounded SkVertices/SkSL
         * mesh shader (SKIA-144-157, CNA_SKIA_SKSL_MESH_V1), composed with this SpriteBatch's
         * active transform matrix exactly like an ordinary sprite draw. An entirely different draw
         * primitive from every `Draw(Texture2D, ...)` overload above, which always submits one
         * quad; @p colors/@p uvs may be null when @p effect's own compiled program needs neither
         * (e.g. a colour-only effect with no texture children).
         *
         * @note CNAEXT -- not part of the XNA 4.0 API. Currently supported only under
         *       `SpriteSortMode::Immediate`: unlike ordinary sprite `Draw()` calls, a mesh draw
         *       does not participate in the deferred sort/batch queue, so it throws if the active
         *       `Begin()` used any other sort mode -- a declared, tested scope boundary, not a
         *       silent misbatch. Currently implemented only by the Skia renderer; every other
         *       renderer's `ISpriteBatchRenderer` throws `std::runtime_error` (matching `Draw()`'s
         *       own existing "renderer does not support this" convention).
         *
         * @param effect      A `ShaderEffect` compiled from `CNA_SKIA_SKSL_MESH_V1` source.
         * @param positions   Per-vertex 2D positions, in the same local space ordinary sprite
         *                    `Draw()` destination rectangles use. Must not be null.
         * @param colors      Optional per-vertex tint colours (straight alpha), or `nullptr`.
         * @param uvs         Optional per-vertex texture coordinates in `[0, 1]`, or `nullptr`.
         * @param vertexCount Number of entries in @p positions/@p colors/@p uvs. Must be positive.
         * @param indices     Triangle-list vertex indices into the arrays above. Must not be null.
         * @param indexCount  Number of entries in @p indices. Must be a positive multiple of 3.
         * @throws std::runtime_error if called before `Begin()`, under a non-`Immediate` sort mode,
         *         with an invalid/non-mesh @p effect, or if @p effect declares a texture child that
         *         was never bound via `ShaderEffect::SetTexture()`.
         * @throws std::invalid_argument if @p positions/@p indices are null or the counts are
         *         non-positive/malformed.
         */
        CNAEXT void DrawMeshEXT(Effect& effect,
                               const Vector2* positions, const Color* colors, const Vector2* uvs,
                               int vertexCount, const std::uint16_t* indices, int indexCount);
    };
}
