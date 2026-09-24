// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace CNA::Internal::Renderers {
    class IGpuTimerRenderer;
}

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Measures how long the GPU spent on a range of commands.
     *
     * plans/plan_modern.md `MOD-2163`. Every number in `docs/cnaext-perf.md` was measured with a CPU wall
     * clock wrapped around a one-texel read-back, which works but measures the wrong thing twice
     * over: the clock starts when the driver *accepts* the work rather than when the GPU starts it,
     * and the read-back that forces completion is itself a synchronisation the real frame would
     * never perform. This asks the GPU.
     *
     * ```cpp
     * CNA::Graphics::GpuTimer timer(device);
     * if (!timer.isSupported()) { … }      // it says why
     * timer.begin();
     * pass.apply(context);
     * timer.end();
     * // …next frame…
     * if (timer.poll()) std::printf("%.3f ms\n", timer.getLastMilliseconds());
     * ```
     *
     * **The result arrives late and that is the point.** A timer whose `end()` waited for its own
     * answer would insert exactly the stall it exists to measure the absence of, so @ref poll never
     * blocks: it returns false until the GPU is finished, which is normally one or two frames after
     * the range closed. A caller wanting a number *now* wants a CPU clock and should say so.
     *
     * **Ranges overlap in flight.** Reopening the timer before the last range's result has landed
     * does not discard that result: up to @ref kRangesInFlight closed ranges wait for the GPU at
     * once, each in its own query, and @ref poll collects them oldest first. A CPU running more
     * than a frame ahead of the GPU is the normal case of a fast renderer without vsync, and with
     * one query the range was reopened every frame before its answer could arrive, so no answer
     * ever did (plans/plan_street_opengl4.md `STREETGL4-0002`). A @ref begin while every query is
     * still waiting times nothing rather than overwrite one.
     *
     * **Where the hardware has no timer query it refuses rather than substituting one.** On GL ES
     * that means `GL_EXT_disjoint_timer_query`, which many drivers — software rasterisers in
     * particular — do not ship. A CPU number wearing a GPU name is worse than no number at all,
     * because it is a measurement of when the driver returned, which is precisely what GPU timing
     * exists to see past.
     */
    class GpuTimer final
    {
    public:
        /** @brief How many closed ranges may wait for the GPU at once. */
        static constexpr std::size_t kRangesInFlight = 4;

        /**
         * @brief Creates a timer, or an unsupported one where the renderer has no timer query.
         *
         * @param device The device to measure on.
         */
        explicit GpuTimer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the timer and its query objects. */
        ~GpuTimer();

        /** @brief Not copyable: it owns a device resource. */
        GpuTimer(const GpuTimer&) = delete;
        /** @brief Not copy-assignable: it owns a device resource. */
        GpuTimer& operator=(const GpuTimer&) = delete;

        /**
         * @brief Whether this timer can measure anything.
         *
         * @return True when the renderer supplied a timer query.
         */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Says why an unsupported timer is unsupported.
         *
         * @return The reason, or an empty string when the timer works.
         */
        [[nodiscard]] const std::string& getUnsupportedReason() const;

        /**
         * @brief Opens the timed range.
         *
         * Does nothing when unsupported or already open, and times nothing when all
         * @ref kRangesInFlight earlier ranges are still waiting for the GPU.
         */
        void begin();

        /**
         * @brief Closes the timed range. Does nothing when unsupported or not open.
         */
        void end();

        /**
         * @brief Whether the GPU has finished the oldest closed range not yet collected.
         *
         * @return True when @ref poll would collect a result without blocking.
         */
        [[nodiscard]] bool isResultAvailable() const;

        /**
         * @brief Collects every finished result, oldest first. Never blocks.
         *
         * @return True when at least one new result was collected, false when the GPU is still
         * working on every closed range or none is waiting.
         */
        bool poll();

        /**
         * @brief The newest collected result, in milliseconds.
         *
         * @return The elapsed GPU time, or 0 before the first result lands.
         */
        [[nodiscard]] double getLastMilliseconds() const;

        /** @brief How many results have been collected since construction. */
        [[nodiscard]] int getSampleCount() const;

        /** @brief Whether a range is currently open. */
        [[nodiscard]] bool isOpen() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_ = nullptr;
        /// A ring of queries, made on demand: a caller whose results land within a frame only ever
        /// owns the first.
        std::array<std::unique_ptr<CNA::Internal::Renderers::IGpuTimerRenderer>, kRangesInFlight>
            queries_;
        /// The query timing the open range.
        std::size_t current_ = 0;
        /// The oldest closed range not yet collected; the waiting ones follow it round the ring.
        std::size_t oldest_ = 0;
        std::size_t pendingCount_ = 0;
        std::string unsupportedReason_;
        double lastMilliseconds_ = 0.0;
        int    sampleCount_      = 0;
        bool   open_             = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
