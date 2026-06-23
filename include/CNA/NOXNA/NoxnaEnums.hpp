// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_NOXNA

namespace CNA {
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

    /** @brief Overall render quality preset. */
    enum class RenderQuality
    {
        /** @brief Minimum quality — maximises frame rate on low-end hardware. */
        Low,
        /** @brief Balanced quality and performance. */
        Medium,
        /** @brief High quality with minor performance cost. */
        High,
        /** @brief Maximum quality regardless of performance cost. */
        Ultra,
    };

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

} // namespace CNA

#endif // CNA_NOXNA
