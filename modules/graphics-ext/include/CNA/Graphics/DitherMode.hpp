// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Ordered-dithering pattern applied by DepthEffect before colour quantization.
     *
     * Error-diffusion dithering (Floyd-Steinberg, Atkinson) is deliberately not offered here:
     * it is inherently sequential (each pixel's quantization error is propagated into
     * not-yet-processed neighbours), which does not parallelize onto a single-pass GPU fragment
     * shader without compute-shader support (tracked long-term as CNAEXT.md task N70). Ordered
     * (Bayer matrix) dithering is single-pass GPU friendly — the same real-time substitute used
     * by RetroArch-style CRT/retro shaders — and is what real-time engines use instead.
     */
    enum class DitherMode
    {
        /** @brief No dithering — flat per-channel/per-luminance quantization (visible banding). */
        None,
        /** @brief 4x4 ordered (Bayer) dithering — coarser, more visible dither cell pattern. */
        Bayer4x4,
        /** @brief 8x8 ordered (Bayer) dithering — finer dither cell pattern. */
        Bayer8x8,
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
