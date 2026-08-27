// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstddef>

namespace CNA::Internal::Renderers::WebGPU
{
    /**
     * @brief WEBGPU-108: the present mode a WebGPU surface configuration should use, as a WGPU-free
     *        choice so the `PresentInterval` -> present-mode policy can be unit-tested without a GPU.
     *
     * The renderer maps each value onto a concrete `WGPUPresentMode` (or, for `FirstAvailable`, onto
     * the surface's first advertised mode).
     */
    enum class PresentModeChoiceEXT
    {
        /** @brief V-sync, tear-free. The WebGPU spec guarantees `Fifo` is always available. */
        Fifo,
        /** @brief No v-sync, may tear -- lowest latency. Chosen for a zero swap interval when offered. */
        Immediate,
        /** @brief No v-sync, tear-free (triple-buffered) -- the fallback for a zero swap interval. */
        Mailbox,
        /** @brief The surface's first advertised present mode -- a defensive fallback only. */
        FirstAvailable
    };

    /**
     * @brief WEBGPU-108: choose a present mode from the requested swap interval and the modes a
     *        surface advertises. Pure and side-effect-free (the testable seam behind
     *        `wgpuSurfaceConfigure.presentMode`).
     *
     * Policy: a zero @p swapInterval (no v-sync) prefers `Immediate`, then `Mailbox`, else `Fifo`;
     * a non-zero swap interval uses `Fifo` (always available per the WebGPU spec), falling back to the
     * surface's first advertised mode only in the defensive case that `Fifo` is somehow absent.
     *
     * @param swapInterval The XNA `PresentInterval`-derived swap interval (0 = no v-sync).
     * @param hasImmediate Whether the surface advertises an immediate (tearing) present mode.
     * @param hasMailbox   Whether the surface advertises a mailbox (triple-buffered) present mode.
     * @param hasFifo      Whether the surface advertises the FIFO (v-sync) present mode.
     * @param hasAny       Whether the surface advertises at least one present mode.
     * @return The chosen @ref PresentModeChoiceEXT.
     */
    [[nodiscard]] CNAEXT inline PresentModeChoiceEXT SelectPresentModeChoiceEXT(
        int swapInterval, bool hasImmediate, bool hasMailbox, bool hasFifo, bool hasAny)
    {
        if (swapInterval == 0)
        {
            if (hasImmediate)
                return PresentModeChoiceEXT::Immediate;
            if (hasMailbox)
                return PresentModeChoiceEXT::Mailbox;
            return PresentModeChoiceEXT::Fifo;
        }
        if (!hasFifo && hasAny)
            return PresentModeChoiceEXT::FirstAvailable;
        return PresentModeChoiceEXT::Fifo;
    }
}
