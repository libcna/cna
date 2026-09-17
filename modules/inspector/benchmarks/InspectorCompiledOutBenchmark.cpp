// SPDX-License-Identifier: MS-PL

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>

int main()
{
    constexpr std::uint64_t iterations = 50000000;
    std::uint64_t value = 1;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i)
    {
        value = value * 1664525U + 1013904223U + i;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto nanoseconds = std::chrono::duration<double, std::nano>(elapsed).count();
    std::cout << "compiled_out_control_ns_per_iteration=" << nanoseconds / iterations
              << " checksum=" << value << '\n';
    return 0;
}
