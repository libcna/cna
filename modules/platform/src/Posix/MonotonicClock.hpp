// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Platform::Posix {

    /**
     * @brief Reads `CLOCK_MONOTONIC` in nanoseconds.
     *
     * Not `CLOCK_REALTIME`: a game loop measures elapsed time, and wall-clock time jumps backwards
     * when NTP corrects it or the user changes the timezone. A backwards jump in a fixed-timestep
     * loop produces either a frozen frame or a burst of catch-up updates. Shared by the X11 and
     * Wayland backends (plans/plan_wayland.md WAYLAND-0014).
     *
     * @return The counter; its frequency is one billion.
     */
    [[nodiscard]] std::uint64_t MonotonicNanoseconds();

    /**
     * @brief Sleeps for a number of milliseconds against `CLOCK_MONOTONIC`.
     *
     * An absolute deadline, so a signal that interrupts the sleep resumes it against the ORIGINAL
     * deadline; the relative form would restart the full duration on every interruption, which
     * turns a 16 ms frame delay into an unbounded one in a process that receives signals.
     *
     * @param milliseconds How long; zero yields the rest of the time slice.
     */
    void SleepMilliseconds(std::uint32_t milliseconds);

} // namespace CNA::Platform::Posix
