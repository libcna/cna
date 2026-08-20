// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Base class for a fullscreen post-process pass.
     *
     * A pass reads one image and writes another. It owns whatever it needs to do that -- its
     * effect, its intermediate targets -- and allocates none of it per frame. Passes are usable on
     * their own: `RenderPipeline` is a convenience that orders them, never a prerequisite.
     *
     * Every pass is capability-gated. Where the active renderer cannot run it, `isSupported()`
     * returns false and `apply()` performs its documented fallback -- for nearly every pass, a
     * pass-through copy -- rather than throwing. A game that enables bloom on a 2D-only renderer
     * gets an unbloomed frame, not an exception.
     */
    class PostProcessPass
    {
    public:
        /** @brief Destroys the pass and its owned GPU resources. */
        virtual ~PostProcessPass() = default;

        PostProcessPass(const PostProcessPass&)            = delete;
        PostProcessPass& operator=(const PostProcessPass&) = delete;

        /**
         * @brief Runs the pass.
         *
         * @param context The images, size, settings and camera parameters for this invocation.
         */
        virtual void apply(const PostProcessContext& context) = 0;

        /**
         * @brief Returns this pass's name, for diagnostics and pipeline statistics.
         *
         * @return A stable, human-readable identifier such as `"Tonemap"`.
         */
        [[nodiscard]] virtual const std::string& getName() const = 0;

        /**
         * @brief Returns whether the active renderer can run this pass.
         *
         * The default answers the question every shader-based pass asks: whether a custom `Effect`
         * can be used at all. A pass with further requirements narrows this.
         *
         * @param device The device whose renderer is queried.
         * @return True when the pass will do its real work rather than its fallback.
         */
        [[nodiscard]] virtual bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

    protected:
        /** @brief Constructs the base of a pass. */
        PostProcessPass() = default;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
