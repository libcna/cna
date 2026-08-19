// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class ShaderEffect;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Splits the colour channels apart towards the edges of the frame.
     *
     * A real lens does not focus every wavelength at the same place, and the error grows with
     * distance from the axis: the centre of the frame is sharp and the corners fringe. This
     * reproduces that by sampling red slightly further out and blue slightly further in, which is
     * why the effect is invisible in the middle of the screen however strong it is set.
     *
     * It runs on displayed pixels, after tonemapping, because it is describing the lens in front of
     * the viewer rather than the one the scene was shot through.
     */
    class ChromaticAberrationPass final : public PostProcessPass
    {
    public:
        /** @brief Creates the pass and compiles its shader. @param device The device to render with. */
        explicit ChromaticAberrationPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Destroys the pass and its shader. */
        ~ChromaticAberrationPass() override;

        /** @brief Fringes @ref PostProcessContext::source into the destination. @param context The images and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"ChromaticAberration"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shader compiled. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns how far the channels separate at the corner of the frame. */
        [[nodiscard]] float getStrength() const;
        /**
         * @brief Sets how far the channels separate at the corner of the frame.
         *
         * @param value The separation as a fraction of the frame, clamped to [0, 0.1].
         */
        void setStrength(float value);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        float strength_ = 0.0f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
