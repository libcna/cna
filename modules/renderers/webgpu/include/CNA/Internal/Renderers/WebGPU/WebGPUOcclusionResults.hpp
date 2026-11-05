// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include "CNA/Internal/Renderers/Common/OcclusionSampleCount.hpp"

namespace CNA::Internal::Renderers::WebGPU
{
    /** @brief Tracks one query interval across deferred draws and resolved render-pass batches. */
    class WebGPUOcclusionResults
    {
    public:
        /** @brief Starts a fresh interval, abandoning the previous tally. */
        void Begin();
        /** @brief Closes the interval without completing unresolved work. */
        void End();
        /** @brief Registers a draw tagged with the current interval. */
        void QueueDraw();
        /** @brief Registers a native batch whose result must arrive before completion. */
        void RecordBatch();
        /** @brief Moves a queued draw into the current native readback batch. */
        void RecordDraw();
        /** @brief Adds a successful native batch readback. @param samples Sum of its query slots. */
        void Resolve(std::uint64_t samples);
        /** @brief Whether End and all draw results have arrived. @return Completion state. */
        [[nodiscard]] bool IsComplete() const;
        /** @brief Whether the interval has ended. @return True after End. */
        [[nodiscard]] bool Ended() const;
        /** @brief Returns the accumulated count or visibility. @param precise Selects tally mode.
         * @return Signed DWORD sample count, or an any-sample flag. */
        [[nodiscard]] int PixelCount(bool precise) const;

    private:
        OcclusionSampleCount count_;
        std::size_t queuedDraws_ = 0;
        std::size_t pendingReadbacks_ = 0;
        bool ended_ = false;
        bool anySamples_ = false;
    };
}
