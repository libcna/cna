// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"

#include <memory>
#include <string>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The identity pass: copies its source to its destination unchanged.
     *
     * Small but load-bearing. It is what every other pass falls back to when the renderer cannot
     * run it, what moves an HDR scene target to the back buffer when no post-processing is
     * enabled, and the reference the chain's own tests compare against -- a chain of blits must
     * reproduce its input exactly, which is the cheapest way to catch an off-by-one in the
     * ping-pong bookkeeping or a flipped texture coordinate.
     */
    class BlitPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass for one device.
         *
         * @param device The device to draw with.
         */
        explicit BlitPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass. */
        ~BlitPass() override;

        /**
         * @brief Copies @ref PostProcessContext::source to @ref PostProcessContext::destination.
         *
         * @param context The images and destination size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"Blit"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns true on every renderer that can draw a textured sprite at all.
         *
         * Unlike the shader passes, a copy needs no custom effect, so this deliberately does not
         * use the base implementation's stricter answer.
         *
         * @param device The device whose renderer is queried.
         * @return True.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
