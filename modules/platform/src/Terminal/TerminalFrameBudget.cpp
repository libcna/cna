// SPDX-License-Identifier: MS-PL

#include "TerminalFrameBudget.hpp"

#include <algorithm>

namespace CNA::Platform::Terminal {

    namespace {

        /// How much unspent budget may accumulate, as seconds of throughput.
        ///
        /// Without a cap, a program that paused for a minute would be allowed a minute's worth of
        /// output in one frame -- which is exactly the blocking write the budget exists to
        /// prevent. One second is enough to absorb an occasional full redraw and no more.
        constexpr double kBurstSeconds = 1.0;

        /// The fastest rate worth recording, in bytes per second.
        ///
        /// A write to a local terminal can return in nanoseconds, which extrapolates to a rate
        /// large enough to lose precision and to make the burst cap meaningless. Anything at or
        /// above this is "no constraint worth modelling" and the exact figure stops mattering.
        constexpr double kMaximumRate = 1.0e9;

    } // namespace

    TerminalFrameBudget::TerminalFrameBudget() = default;

    void TerminalFrameBudget::Refill(const std::uint64_t nowNanoseconds)
    {
        if (!started_)
        {
            started_ = true;
            lastRefillNanoseconds_ = nowNanoseconds;
            available_ = bytesPerSecond_ * kBurstSeconds;
            return;
        }

        if (nowNanoseconds <= lastRefillNanoseconds_)
        {
            return;
        }
        const double elapsedSeconds =
            static_cast<double>(nowNanoseconds - lastRefillNanoseconds_) / 1.0e9;
        lastRefillNanoseconds_ = nowNanoseconds;
        available_ = std::min(available_ + bytesPerSecond_ * elapsedSeconds,
                              bytesPerSecond_ * kBurstSeconds);
    }

    bool TerminalFrameBudget::CanAfford(const std::size_t bytes, const std::uint64_t nowNanoseconds)
    {
        if (bytesPerSecond_ <= 0.0)
        {
            // Nothing measured yet, so there is nothing to enforce. Refusing here would drop the
            // very first frame -- the one that establishes what the link can do.
            return true;
        }

        Refill(nowNanoseconds);
        if (available_ < static_cast<double>(bytes))
        {
            return false;
        }
        available_ -= static_cast<double>(bytes);
        return true;
    }

    void TerminalFrameBudget::ObserveWrite(const std::size_t bytes,
                                           const std::uint64_t durationNanoseconds)
    {
        if (bytes == 0)
        {
            return;
        }

        const double seconds = static_cast<double>(durationNanoseconds) / 1.0e9;
        const double observed =
            seconds > 0.0 ? std::min(static_cast<double>(bytes) / seconds, kMaximumRate)
                          : kMaximumRate;

        const bool firstMeasurement = recentCount_ == 0;
        recent_[recentNext_] = observed;
        recentNext_ = (recentNext_ + 1) % kWindow;
        recentCount_ = std::min(recentCount_ + 1, kWindow);
        RecomputeRate();

        if (firstMeasurement)
        {
            available_ = bytesPerSecond_ * kBurstSeconds;
        }
    }

    void TerminalFrameBudget::RecomputeRate()
    {
        double sorted[kWindow];
        for (std::size_t i = 0; i < recentCount_; ++i)
        {
            sorted[i] = recent_[i];
        }
        std::sort(sorted, sorted + recentCount_);
        bytesPerSecond_ = sorted[recentCount_ / 2];
    }

    void TerminalFrameBudget::SetBytesPerSecondForTesting(const double bytesPerSecond)
    {
        bytesPerSecond_ = std::max(0.0, bytesPerSecond);
        available_ = bytesPerSecond_ * kBurstSeconds;
        started_ = false;
        // The window is cleared too: an override that left old observations in place would be
        // silently undone by the next real write.
        recentCount_ = 0;
        recentNext_ = 0;
        for (double& sample : recent_)
        {
            sample = bytesPerSecond_;
        }
        if (bytesPerSecond_ > 0.0)
        {
            recentCount_ = kWindow;
        }
    }

} // namespace CNA::Platform::Terminal
