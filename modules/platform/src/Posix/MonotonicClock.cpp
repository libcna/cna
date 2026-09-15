// SPDX-License-Identifier: MS-PL

#include "MonotonicClock.hpp"

#include <cerrno>
#include <ctime>

namespace CNA::Platform::Posix {

    std::uint64_t MonotonicNanoseconds()
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return static_cast<std::uint64_t>(now.tv_sec) * 1000000000uLL + static_cast<std::uint64_t>(now.tv_nsec);
    }

    void SleepMilliseconds(const std::uint32_t milliseconds)
    {
        if (milliseconds == 0)
        {
            // Zero means "yield the rest of the slice", which is a zero-length sleep rather than a
            // no-op: a busy-wait loop calling Delay(0) must still let another thread run.
            timespec zero{0, 0};
            nanosleep(&zero, nullptr);
            return;
        }
        timespec deadline{};
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += static_cast<time_t>(milliseconds / 1000u);
        deadline.tv_nsec += static_cast<long>((milliseconds % 1000u) * 1000000uL);
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_nsec -= 1000000000L;
            ++deadline.tv_sec;
        }
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) == EINTR)
        {
        }
    }

} // namespace CNA::Platform::Posix
