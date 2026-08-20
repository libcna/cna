// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"

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
     * @brief Binds a render target for a scope and puts back whatever was bound before.
     *
     * plan_modern.md `MOD-203`. A post-process pass binds its destination, draws, and moves on —
     * and if the draw throws, the destination stays bound. The next thing to render then draws into
     * a pass's private intermediate instead of the frame, which does not look like an error; it
     * looks like the frame stopped updating. Restoring in a destructor makes the unwind path do the
     * right thing without every pass remembering to.
     *
     * The previous binding is read back from the device rather than assumed to be the back buffer,
     * because a pass can perfectly well run inside another pass's scope — the bloom pyramid does
     * exactly that — and "restore" has to mean the *caller's* target, not the frame's.
     *
     * Restoration in the destructor never throws: a renderer that refuses the restore has already
     * failed at something, and turning that into a second exception during unwind would replace a
     * diagnosable error with `std::terminate`.
     */
    class ScopedRenderTarget
    {
    public:
        /**
         * @brief Records the current binding and binds @p destination in its place.
         *
         * @param device      The device to bind on.
         * @param destination The target to bind, or null for the back buffer.
         */
        ScopedRenderTarget(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                           Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination);

        /** @brief Restores the binding recorded at construction. */
        ~ScopedRenderTarget();

        ScopedRenderTarget(const ScopedRenderTarget&)            = delete;
        ScopedRenderTarget& operator=(const ScopedRenderTarget&) = delete;

        /**
         * @brief Whether a binding was recorded and will be restored.
         *
         * False when the device could not report what was bound — some renderers do not track it —
         * in which case the destructor restores the back buffer instead, which is what the previous
         * unscoped code did unconditionally.
         *
         * @return True when the recorded binding is the one that will be put back.
         */
        [[nodiscard]] bool hasRecordedPrevious() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::vector<Microsoft::Xna::Framework::Graphics::RenderTargetBinding> previous_;
        bool recorded_ = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
