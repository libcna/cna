// SPDX-License-Identifier: MS-PL
#include "CNA/Diagnostics/Diagnostics.hpp"
#include "CNA/Diagnostics/Instrumentation.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace
{
    constexpr std::uint64_t Iterations = 5'000'000;
    constexpr std::uint64_t BatchSize = 512;

    template<typename Operation>
    double Measure(std::string_view name, Operation operation)
    {
        std::uint64_t value = 0x123456789abcdef0ULL;
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t index = 0; index < Iterations; ++index)
        {
            value = value * 6364136223846793005ULL + index;
            operation();
#if defined(__GNUC__) || defined(__clang__)
            asm volatile("" : "+r"(value) : : "memory");
#endif
        }
        const auto end = std::chrono::steady_clock::now();
        const double nanoseconds = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        std::cout << std::left << std::setw(24) << name << std::right << std::fixed
                  << std::setprecision(3) << (nanoseconds / static_cast<double>(Iterations))
                  << " ns/op\n";
        return nanoseconds;
    }

    template<typename Operation, typename BetweenBatches>
    double MeasureBatched(std::string_view name, Operation operation,
                          BetweenBatches betweenBatches)
    {
        std::uint64_t value = 0x123456789abcdef0ULL;
        std::chrono::nanoseconds elapsed{};
        for (std::uint64_t first = 0; first < Iterations; first += BatchSize)
        {
            const std::uint64_t endIndex = std::min(first + BatchSize, Iterations);
            const auto start = std::chrono::steady_clock::now();
            for (std::uint64_t index = first; index < endIndex; ++index)
            {
                value = value * 6364136223846793005ULL + index;
                operation();
#if defined(__GNUC__) || defined(__clang__)
                asm volatile("" : "+r"(value) : : "memory");
#endif
            }
            const auto end = std::chrono::steady_clock::now();
            elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            betweenBatches();
        }
        const double nanoseconds = static_cast<double>(elapsed.count());
        std::cout << std::left << std::setw(24) << name << std::right << std::fixed
                  << std::setprecision(3) << (nanoseconds / static_cast<double>(Iterations))
                  << " ns/op\n";
        return nanoseconds;
    }
}

int main()
{
    using namespace CNA::Diagnostics;
    (void)SetRuntimeMode(GetBuildMode());
    std::cout << "CNA_DIAGNOSTICS_LEVEL=" << CNA_DIAGNOSTICS_LEVEL
              << " iterations=" << Iterations << '\n';

    const double baseline = Measure("baseline", [] {});
    const double counter = Measure("frame counter", [] {
        CNA_DIAGNOSTICS_FRAME_COUNTER_ADD("Benchmark/FrameCounter", 1);
    });
    std::uint64_t cursor = GetProvider().ReadEvents(0, EventHistoryCapacity).newestAvailableSequence;
    const double zone = MeasureBatched(
        "scoped zone",
        [] { CNA_PROFILE_SCOPE("Benchmark/Zone"); },
        [&cursor] {
            const EventBatch events = GetProvider().ReadEvents(cursor, BatchSize + 16);
            cursor = events.newestAvailableSequence;
        });
    std::cout << "counter overhead         " << std::fixed << std::setprecision(3)
              << ((counter - baseline) / static_cast<double>(Iterations)) << " ns/op\n"
              << "zone overhead            "
              << ((zone - baseline) / static_cast<double>(Iterations)) << " ns/op\n";
    return 0;
}
