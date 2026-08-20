// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /** @brief Shadow map quality preset. */
    enum class ShadowQuality
    {
        /** @brief Shadows disabled. */
        Disabled,
        /** @brief 512×512 shadow map, no filtering. */
        Low,
        /** @brief 1024×1024 shadow map, PCF 2×2. */
        Medium,
        /** @brief 2048×2048 shadow map, PCF 3×3. */
        High,
        /** @brief 4096×4096 shadow map, PCF 5×5. */
        Ultra,
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
