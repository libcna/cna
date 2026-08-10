// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

    /** @brief Tonemapping operator applied to the HDR framebuffer before display. */
    enum class TonemappingMode
    {
        /** @brief No tonemapping — HDR values are clamped to [0,1]. */
        None,
        /** @brief Reinhard tonemapping (simple, slightly desaturates highlights). */
        Reinhard,
        /** @brief Filmic tonemapping (S-curve, warm highlights). */
        Filmic,
        /** @brief ACES filmic tonemapping (physically-based, cinema standard). */
        Aces,
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
