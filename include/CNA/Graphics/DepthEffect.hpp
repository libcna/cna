// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_NOXNA

#include "CNA/Graphics/DepthEffectMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

namespace CNA::Graphics {

    /**
     * @brief Full-screen colour-depth-reduction post-process effect.
     *
     * A `ShaderEffect` (GLSL, EasyGL backend) that quantizes the rendered colour to a
     * fixed number of levels per channel, emulating limited-palette display hardware:
     * 16-bit (RGB565) colour, 8-bit (RGB332) colour, or 4-bit/2-bit/1-bit greyscale.
     *
     * Usage mirrors any other custom `ShaderEffect`-based post-process — draw the scene
     * into a `Texture2D` (typically via a `RenderTarget2D`), then redraw it through
     * `SpriteBatch::Begin(sortMode, blendState, sampler, nullptr, nullptr, &depthEffect)`.
     *
     * NOXNA extension — no XNA/FNA precedent; this is a from-scratch CNA post-process
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

        /** @brief Returns the fully qualified CNA type name. */
        [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Creates an independent clone of this effect with the same mode.
         *
         * @return Pointer to the cloned effect. Caller takes ownership.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Effect* Clone() override;

    protected:
        /** @brief Binds the compiled shader and uploads the current colour-depth mode. */
        void OnApply() override;

    private:
        DepthEffectMode mode_ = DepthEffectMode::Color16Bit;
    };

} // namespace CNA::Graphics

#endif // CNA_NOXNA
