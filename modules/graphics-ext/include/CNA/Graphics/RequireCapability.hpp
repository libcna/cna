// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics::detail {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Returns a human-readable name for a capability, for use in messages.
     *
     * @param capability The capability to name.
     * @return A short noun phrase, e.g. `"float render targets"`.
     */
    [[nodiscard]] std::string nameOfCapability(CNA::GraphicsCapability capability);

    /**
     * @brief Returns, or throws @ref CNA::Graphics::EngineException, if a capability is missing.
     *
     * plans/plan_modern.md `MOD-10`. The single choke point for "this subsystem needs something the
     * renderer does not have". Written once so the sentence is the same everywhere and so the
     * renderer's name is always resolved from the device rather than guessed at the call site —
     * "not supported" without naming the renderer is the least useful log line in graphics.
     *
     * Most of the engine layer does **not** call this: a pass that can degrade reports
     * `isSupported() == false` and copies its input instead. This is for the paths where carrying
     * on would produce a silently wrong frame.
     *
     * @param device     The device to ask.
     * @param capability The capability the subsystem needs.
     * @param subsystem  The caller's name, used in the message, e.g. `"BloomPass"`.
     * @throws CNA::Graphics::EngineException If @p device does not support @p capability.
     */
    void requireCapability(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                           CNA::GraphicsCapability capability, const std::string& subsystem);

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT
