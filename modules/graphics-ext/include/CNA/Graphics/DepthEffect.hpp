// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Full-screen colour-depth-reduction post-process effect.
     *
     * A `ShaderEffect` (GLSL, EasyGL renderer) that quantizes the rendered colour to a
     * fixed number of levels per channel, emulating limited-palette display hardware:
     * 16-bit (RGB565) colour, 8-bit (RGB332) colour, 4-bit/2-bit/1-bit greyscale, or a
     * real nearest-colour palette match against a fixed 216-colour "web-safe" palette
     * (Palette256) or the classic 16-colour EGA/CGA palette (Palette16) — unlike the bit-depth
     * modes above, these search an actual non-uniform colour table instead of rounding each
     * channel independently. Optionally applies ordered (Bayer matrix) dithering before
     * quantization/matching — see DitherMode — to break up flat banding the way
     * period-authentic limited-palette hardware display shaders do.
     *
     * Usage mirrors any other custom `ShaderEffect`-based post-process — draw the scene
     * into a `Texture2D` (typically via a `RenderTarget2D`), then redraw it through
     * `SpriteBatch::Begin(sortMode, blendState, sampler, nullptr, nullptr, &depthEffect)`.
     *
     * CNAEXT extension — no XNA/FNA precedent; this is a from-scratch CNA post-process
     * effect, not a port.
     */
    class DepthEffect : public Microsoft::Xna::Framework::Graphics::ShaderEffect
    {
    public:
        /**
         * @brief Constructs a DepthEffect, compiling its built-in GLSL shader.
         *
         * @param device GraphicsDevice that owns this effect.
         */
        explicit DepthEffect(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Returns the active colour-depth mode. */
        [[nodiscard]] DepthEffectMode getMode() const;

        /** @brief Sets the colour-depth mode applied on the next Apply(). */
        void setMode(DepthEffectMode mode);

        /** @brief Returns the active ordered-dithering pattern. */
        [[nodiscard]] DitherMode getDitherMode() const;

        /** @brief Sets the ordered-dithering pattern applied on the next Apply(). */
        void setDitherMode(DitherMode mode);

        /** @brief Returns the fully qualified CNA type name. */
        [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Creates an independent clone of this effect with the same mode.
         *
         * @return Pointer to the cloned effect. Caller takes ownership.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Effect* Clone() override;

    protected:
        /** @brief Binds the compiled shader and uploads the current colour-depth/dither mode. */
        void OnApply() override;

    private:
        /**
         * @brief Lazily builds the Palette256/Palette16 lookup textures on first use.
         *
         * No-op if the textures already exist or if this effect has no working renderer
         * (mirrors ShaderEffect's own "no device.renderer_" tolerance — building a texture
         * with GraphicsDevice::GetRenderer() throws when there is no renderer).
         */
        void ensurePaletteTextures();

        DepthEffectMode mode_ = DepthEffectMode::Color16Bit;
        DitherMode ditherMode_ = DitherMode::None;
        Microsoft::Xna::Framework::Graphics::Texture2D palette256Texture_;
        Microsoft::Xna::Framework::Graphics::Texture2D palette16Texture_;
        bool paletteTexturesBuilt_ = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
