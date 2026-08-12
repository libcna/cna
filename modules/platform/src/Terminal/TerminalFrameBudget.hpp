// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>

namespace CNA::Platform::Terminal {

    /**
     * @brief Decides whether the terminal can afford this frame, and drops it if not.
     *
     * ### Why a budget and not a frame rate
     *
     * The link to a terminal has no knowable speed. A local pseudo-terminal absorbs megabytes; the
     * same program over ssh on a bad connection may manage tens of kilobytes a second. A fixed
     * frame rate is therefore the wrong contract: too low wastes a fast terminal, too high makes a
     * slow one worse, because the write **blocks** and drags the game's own frame rate down with
     * it. Dropping frames is the honest failure — the picture updates less often and everything
     * else keeps running.
     *
     * ### The rate is measured, not configured
     *
     * There is no constant here to get wrong. A write that returns immediately means the terminal
     * kept up; a write that blocks means it did not, and *how long* it blocked is exactly the
     * link's throughput. So the budget observes its own writes and refills at the rate it last
     * achieved. On a fast terminal the measured rate is enormous and the budget never binds; on a
     * slow one it falls to what the link really does, and frames start being dropped instead of
     * queued.
     *
     * The rate is the **median** of the last few writes rather than a running average. The two
     * demands on it pull in opposite directions: a single slow write — a scheduler hiccup, a
     * moment of contention — must not throttle a fast terminal, while a link that genuinely
     * degraded must be believed within a handful of frames. An exponential average cannot do both,
     * because one weight controls both behaviours; a median ignores an outlier entirely and
     * follows a sustained change as soon as it fills half the window.
     *
     * ### Dropping a frame is safe only because of damage tracking
     *
     * A dropped frame is not lost. `TerminalSurfacePresenter` leaves its record of what is on
     * screen untouched, so the next frame's diff (PLAT-133) covers everything that changed since
     * the last frame that was actually drawn. Without that, dropping would leave the screen
     * permanently showing a stale picture.
     */
    class TerminalFrameBudget
    {
    public:
        /** @brief Creates a budget that has not yet measured anything. */
        TerminalFrameBudget();

        /**
         * @brief Reports whether a frame of this size fits in the budget right now.
         *
         * @param bytes The frame's size.
         * @param nowNanoseconds The current time from a monotonic clock.
         * @return True if the frame should be written; false if it should be dropped.
         */
        [[nodiscard]] bool CanAfford(std::size_t bytes, std::uint64_t nowNanoseconds);

        /**
         * @brief Records that a frame was written, and how long the write took.
         *
         * The duration is the measurement: a write that blocked reveals the link's real
         * throughput, and a write that returned at once reveals that there is no constraint worth
         * modelling.
         *
         * @param bytes How many bytes were written.
         * @param durationNanoseconds How long the write took.
         */
        void ObserveWrite(std::size_t bytes, std::uint64_t durationNanoseconds);

        /**
         * @brief Gets the throughput the budget currently believes in.
         *
         * @return Bytes per second, or zero before anything has been measured.
         */
        [[nodiscard]] double GetMeasuredBytesPerSecond() const { return bytesPerSecond_; }

        /**
         * @brief Overrides the measured rate.
         *
         * For tests, which cannot make a pseudo-terminal slow, and for a caller that knows
         * something about its link that measurement cannot discover in time.
         *
         * @param bytesPerSecond The rate to assume; zero restores "unmeasured", which allows
         *        everything.
         */
        void SetBytesPerSecondForTesting(double bytesPerSecond);

    private:
        void Refill(std::uint64_t nowNanoseconds);
        void RecomputeRate();

        /// The last few observed rates, oldest first. Small on purpose: a long window would take
        /// too many frames to follow a link that really changed.
        static constexpr std::size_t kWindow = 5;
        double recent_[kWindow] = {};
        std::size_t recentCount_ = 0;
        std::size_t recentNext_ = 0;

        double bytesPerSecond_ = 0.0;  ///< Zero means unmeasured, which allows every frame.
        double available_ = 0.0;
        std::uint64_t lastRefillNanoseconds_ = 0;
        bool started_ = false;
    };

} // namespace CNA::Platform::Terminal
