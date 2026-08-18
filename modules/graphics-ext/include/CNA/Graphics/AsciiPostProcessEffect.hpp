// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; }

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Renderer-neutral ASCII/glyph-grid image post-process effect.
     *
     * Quantizes an already-rendered image (any `Texture2D`, typically a `RenderTarget2D` that a
     * normal 2D or 3D scene was drawn into) into a glyph/color grid -- one tinted textured quad
     * per cell, sampled from a small hand-authored glyph-density font atlas -- and draws the
     * result wherever the owning `GraphicsDevice`'s render target currently points (the real
     * backbuffer, or another `RenderTarget2D`):
     *
     * @code
     * device.SetRenderTarget2D(sceneTarget.get());
     * // ... draw a normal 2D or 3D scene into sceneTarget ...
     * device.SetRenderTarget2D(nullptr); // back to the real backbuffer
     * asciiEffect.Draw(*sceneTarget);
     * @endcode
     *
     * This is the direct successor to the former `ASCII` graphics-renderer identity
     * (`CNA_GRAPHICS_RENDERER=ASCII`, removed): that renderer only ever quantized its OWN 2D-only
     * native 2D-renderer-composited frame before presenting it. As a post-process effect instead, the
     * exact same quantization/glyph-atlas logic applies to the completed output of ANY renderer
     * and ANY scene (2D or 3D) -- the effect only ever reads finished pixels back via the public
     * `Texture2D::GetData()`/`SpriteBatch` APIs, never a renderer-internal type.
     *
     * @note Not a genuine terminal/TTY renderer -- this produces an ASCII-*looking* graphical
     * framebuffer (real textured quads on a real render target), not actual character-cell
     * terminal output. A genuine terminal/TTY renderer, if ever built, would be a separate,
     * unrelated concept.
     *
     * @note Portable CPU implementation (v1): `Draw()` reads the source texture back to the CPU
     * (`Texture2D::GetData()`), quantizes it there, then re-uploads the result as textured quads
     * via `SpriteBatch`. This is a real GPU-to-CPU readback every call, same cost class the former
     * ASCII renderer's own `Present()` already paid every frame. A shader-only GPU path (source
     * texture -> fragment shader -> cell luminance/color selection -> glyph atlas sampling, no
     * per-frame readback) is architecturally possible on shader-capable renderers via
     * `Microsoft::Xna::Framework::Graphics::ShaderEffect`, but is deliberately deferred -- it would
     * not run on 2D-only/non-shader renderers (`SDL_RENDERER`, `HEADLESS`, `SOFTWARE`, ...), which
     * this effect must keep supporting uniformly.
     *
     * CNAEXT extension -- no XNA/FNA precedent. Deliberately not named `AsciiEffect` alone: unlike
     * `Microsoft::Xna::Framework::Graphics::Effect` (a GPU shader program `SpriteBatch`/
     * `GraphicsDevice` binds and applies), this type performs its own multi-step CPU+GPU pass and
     * cannot be bound as a shader `Effect`.
     */
    class AsciiPostProcessEffect
    {
    public:
        /**
         * @brief Constructs the effect, uploading its built-in glyph-density font atlas.
         *
         * @param device GraphicsDevice that owns the font atlas texture and the internal
         *               SpriteBatch used to draw the quantized grid.
         */
        explicit AsciiPostProcessEffect(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /**
         * @brief Sets the source-pixel block size `Draw()` averages into one glyph cell.
         *
         * Independent of the font atlas' own fixed 8x8 glyph texture size: the grid cell's
         * on-screen quad is always stretched to fill its share of the destination rectangle,
         * whatever that resolves to, so a coarser/finer cell size here just changes how many
         * cells there are, not how the atlas is sampled. Defaults to 8x8.
         *
         * @param width  Source pixels per grid cell, horizontally. Must be positive.
         * @param height Source pixels per grid cell, vertically. Must be positive.
         * @throws std::invalid_argument if @p width or @p height is not positive.
         */
        void setCellSize(int width, int height);

        /** @brief Retrieves the current source-pixel block size set by setCellSize(). */
        void getCellSize(int& width, int& height) const;

        /** @brief Returns the active quantization mode. */
        [[nodiscard]] AsciiQuantizeMode getQuantizeMode() const;

        /** @brief Sets the quantization mode used by the next Draw() call. */
        void setQuantizeMode(AsciiQuantizeMode mode);

        /**
         * @brief Quantizes @p source and draws the resulting glyph/color grid stretched to fill
         * the entire current viewport of the owning GraphicsDevice's currently-bound render
         * target.
         *
         * @param source Already-rendered image to quantize (e.g. a `RenderTarget2D` a 2D or 3D
         *               scene was drawn into). Read back via `Texture2D::GetData()`.
         */
        void Draw(Microsoft::Xna::Framework::Graphics::Texture2D& source);

        /**
         * @brief Quantizes @p source and draws the resulting glyph/color grid stretched to fill
         * @p destinationRectangle of whatever render target is currently bound.
         *
         * @param source                Already-rendered image to quantize.
         * @param destinationRectangle  Destination rectangle, in pixels of the currently-bound
         *                              render target.
         */
        void Draw(Microsoft::Xna::Framework::Graphics::Texture2D& source,
                  const Microsoft::Xna::Framework::Rectangle& destinationRectangle);

        /**
         * @brief Reports the glyph grid's column/row count as of the most recent Draw() call.
         *
         * Exposed for testing -- confirms setCellSize()/the source image's own dimensions
         * actually changed what Draw() produced, not just that the setter/getter round-trip.
         */
        void GetLastGridDimensions(int& columns, int& rows) const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_;
        Microsoft::Xna::Framework::Graphics::Texture2D fontAtlasTexture_;
        Microsoft::Xna::Framework::Graphics::SpriteBatch spriteBatch_;
        int cellWidth_;
        int cellHeight_;
        AsciiQuantizeMode mode_;
        int lastGridColumns_ = 0;
        int lastGridRows_ = 0;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
