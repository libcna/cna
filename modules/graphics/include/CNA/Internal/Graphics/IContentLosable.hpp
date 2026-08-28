// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Graphics
{
    /**
     * @brief Implemented by the resource types whose contents a device reset can destroy.
     *
     * plans/plan_cabi.md CABI-15. XNA raises ContentLost on the default-pool resources -- dynamic
     * vertex/index buffers and render targets -- when a device is reset out from under them. CNA
     * has that event for real on the renderers whose API can lose a device (DirectX9, Direct2D,
     * Skia); the other families never report one and so never raise this.
     *
     * The device walks its own resource list and asks each entry whether it is losable, rather
     * than testing four concrete types at the call site.
     */
    class IContentLosable
    {
    public:
        virtual ~IContentLosable() = default;

        /**
         * @brief Marks the content lost and raises the type's own ContentLost event.
         *
         * Called only when a renderer actually reported a device reset. Raising it on a schedule,
         * or on a caller-initiated reset that no renderer lost anything across, would replace one
         * untruth with a louder one.
         */
        virtual void NotifyContentLostEXT() = 0;

        /**
         * @brief Clears the lost flag because the caller has taken the content back.
         *
         * For a buffer that means a `SetData`; for a render target it means being bound for
         * rendering. The two differ because a render target has no renderer-neutral "a pixel was
         * written" signal below this API, while a bound target with the default discard usage has
         * already lost its previous contents by definition.
         *
         * The other half of the contract, and the half that was missing: a flag that is set and
         * never cleared reports "lost" forever, which is a different untruth from never raising it
         * at all. Every implementer had this method already; declaring it here is what lets the
         * write paths call it without knowing the concrete type.
         */
        virtual void ClearContentLostEXT() noexcept = 0;
    };
}
