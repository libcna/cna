// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief How `DepthNormalPrepass` stores linear depth.
     *
     * plan_modern.md `MOD-2035`. The layer packs depth into an 8-bit target everywhere, and the
     * reason is a measurement rather than a preference: with a half-float target, screen-space
     * effects driven from the prepass occluded *nothing* on the reference renderer. **Why** is still
     * open, and a policy that cannot be compared against its alternative can never be re-examined —
     * so the alternative stays reachable, for a diagnostic that wants to build the failing shape and
     * for a renderer that one day proves it does not fail there.
     */
    enum class DepthEncoding
    {
        /** @brief Whatever `DepthNormalPrepass::usesPackedDepthEXT` decides. The default. */
        Automatic,

        /** @brief 32 bits of depth across an 8-bit RGBA target; needs no capability. */
        Packed,

        /** @brief One half-float channel. **Known to defeat this layer's screen-space effects.** */
        HalfFloat
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
