// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Keeps intermediate render targets alive across frames, keyed by their shape.
     *
     * A post-process chain wants several intermediate images per frame, and a bloom pyramid wants
     * one per mip level. Allocating those per frame would mean creating and destroying GPU
     * textures inside the frame loop -- the cost is not the allocation but the driver-side
     * synchronisation it can force. The pool hands out a target matching a requested shape, and
     * hands back the same object next frame.
     *
     * Targets are owned by the pool and outlive any single `acquire()`; they are released only by
     * `reset()` or by destroying the pool. This is deliberately not a free-list with recycling
     * inside a frame: two passes asking for the same shape in one frame are usually ping-ponging
     * and must get *different* targets, which is what the `slot` parameter of `acquire()` is for.
     */
    class RenderTargetPool
    {
    public:
        /**
         * @brief Creates an empty pool for one device.
         *
         * @param device The device every pooled target is created on.
         */
        explicit RenderTargetPool(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pool and every target it owns. */
        ~RenderTargetPool();

        RenderTargetPool(const RenderTargetPool&)            = delete;
        RenderTargetPool& operator=(const RenderTargetPool&) = delete;

        /**
         * @brief Returns a target of the requested shape, creating it on first request.
         *
         * @param width       Width in pixels; must be positive.
         * @param height      Height in pixels; must be positive.
         * @param format      Colour format.
         * @param depthFormat Depth format, `None` for a colour-only target.
         * @param slot        Distinguishes several targets of one shape (ping-pong sides, pyramid
         *                    levels). Requests differing only in slot return different targets.
         * @return A target owned by this pool, valid until `reset()` or destruction.
         * @throws std::invalid_argument If @p width or @p height is not positive.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::RenderTarget2D* acquire(
            int width, int height,
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format,
            Microsoft::Xna::Framework::Graphics::DepthFormat depthFormat,
            int slot = 0);

        /** @brief Releases every pooled target. Call on resize, or to reclaim GPU memory. */
        void reset();

        /**
         * @brief Returns how many targets the pool currently owns.
         *
         * Exposed because "allocates nothing in steady state" is a promise this layer makes, and a
         * promise nothing can observe is a promise nothing can keep.
         *
         * @return The number of live pooled targets.
         */
        [[nodiscard]] std::size_t getTargetCount() const;

        /**
         * @brief Returns the total GPU bytes of every pooled target, as an estimate.
         *
         * Colour storage only, computed from each target's format and size; depth and multisample
         * storage are not included because their real cost is driver-defined.
         *
         * @return Estimated bytes.
         */
        [[nodiscard]] std::size_t getEstimatedBytes() const;

    private:
        struct Entry;

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::vector<std::unique_ptr<Entry>> entries_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
