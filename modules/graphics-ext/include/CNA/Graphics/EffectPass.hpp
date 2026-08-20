// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class Effect;
    class GraphicsDevice;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Runs any `Effect` as a fullscreen post-process pass.
     *
     * plans/plan_modern.md `MOD-232`. The adapter that makes the CNAEXT effects that existed before this
     * plan — `DepthEffect`, `CRTEffect`, and any `ShaderEffect` a game writes — usable inside a
     * `PostProcessChain` or a `RenderPipeline` without changing them at all. They are `Effect`
     * subclasses, and a fullscreen pass is exactly "draw the source through an effect", so the
     * adapter is the whole adaptation: no wrapper per effect type, and none needed for the next one.
     *
     * It is equally the shortest route to a custom pass. A game with a shader and no interest in
     * subclassing anything can hand it here and be in the chain.
     */
    class EffectPass : public PostProcessPass
    {
    public:
        /**
         * @brief Creates a pass that runs a borrowed effect.
         *
         * @param device The device the pass draws on.
         * @param effect The effect to run. Not owned; it must outlive the pass. May be null, in
         *               which case the pass copies its input, which is the same fallback every
         *               unsupported pass takes.
         * @param name   The pass's name, for diagnostics and pipeline statistics.
         */
        EffectPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                   Microsoft::Xna::Framework::Graphics::Effect* effect, std::string name);

        /**
         * @brief Creates a pass that owns its effect.
         *
         * @param device The device the pass draws on.
         * @param effect The effect to run and to own.
         * @param name   The pass's name.
         */
        EffectPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                   std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> effect,
                   std::string name);

        /** @brief Destroys the pass, and its effect if it owned one. */
        ~EffectPass() override;

        /**
         * @brief Draws the source through the effect into the destination.
         *
         * @param context The images and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief The pass's name. @return The name given at construction. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Whether the effect will actually shade the draw.
         *
         * @param device The device whose renderer is queried.
         * @return True when an effect is attached and the renderer runs custom effects.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief The effect being run. @return The effect, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Effect* getEffect() const;

        /**
         * @brief Replaces the effect with a borrowed one, releasing any owned one.
         *
         * @param effect The effect to run from now on, or null to make the pass a copy.
         */
        void setEffect(Microsoft::Xna::Framework::Graphics::Effect* effect);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> ownedEffect_;
        Microsoft::Xna::Framework::Graphics::Effect* effect_ = nullptr;
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::string name_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
