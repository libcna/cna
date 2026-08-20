// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessPass.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics {

    class AsciiPostProcessEffect;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Runs `AsciiPostProcessEffect` as a post-process pass.
     *
     * plans/plan_modern.md `MOD-232`. The one of the three pre-existing CNAEXT effects that `EffectPass`
     * cannot carry, and the reason is worth knowing rather than working around: it is not an
     * `Effect`. It reads the source back to the CPU, quantises it into a glyph grid, and re-uploads
     * the result as one textured quad per cell — a multi-step CPU+GPU pass, not a shader program
     * `SpriteBatch` can bind. That is also what lets it run on renderers with no shaders at all,
     * which is the property it was built for.
     *
     * Two consequences a chain author should expect. It **reads a texture back every frame**, which
     * is a real GPU-to-CPU round trip and much the most expensive pass in this layer; and it needs
     * that readback to work at all, so `isSupported()` asks the renderer by probing rather than by
     * reading a capability, since no capability describes it.
     */
    class AsciiPass : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and the effect it drives.
         *
         * @param device The device the pass draws on.
         */
        explicit AsciiPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its effect. */
        ~AsciiPass() override;

        /**
         * @brief Quantises the source into the destination as a glyph grid.
         *
         * @param context The images and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief The pass's name. @return `"Ascii"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Whether this renderer can read a texture back to the CPU, which the pass requires.
         *
         * @param device The device to ask.
         * @return True when the readback the effect depends on works here.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /**
         * @brief The effect being driven, so its cell size and quantiser can be configured.
         *
         * @return The effect, owned by this pass.
         */
        [[nodiscard]] AsciiPostProcessEffect& getEffect() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<AsciiPostProcessEffect> effect_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
