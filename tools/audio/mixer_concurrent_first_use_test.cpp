// SPDX-License-Identifier: MS-PL
//
// AUDIO-002 regression: exercise the shared SDL3 mixer from a fresh process, with every worker
// crossing the first-use boundary at once. The permanent mutex contract is stronger than merely
// avoiding a crash: every successful caller must observe the same fully-created mixer.

#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <SDL3_mixer/SDL_mixer.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    constexpr std::size_t kThreadCount = 16;
    std::barrier start(static_cast<std::ptrdiff_t>(kThreadCount));
    std::array<MIX_Mixer*, kThreadCount> mixers{};
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::size_t index = 0; index < kThreadCount; ++index)
    {
        workers.emplace_back([&, index]
        {
            start.arrive_and_wait();
            try
            {
                mixers[index] = CNA::Internal::Audio::GetMixer();
            }
            catch (...)
            {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) worker.join();

    const MIX_Mixer* expected = mixers.front();
    bool sameMixer = expected != nullptr && failures.load(std::memory_order_relaxed) == 0;
    for (const MIX_Mixer* mixer : mixers) sameMixer = sameMixer && mixer == expected;

    SDL_AudioSpec format{};
    const bool validFormat = expected != nullptr
        && MIX_GetMixerFormat(const_cast<MIX_Mixer*>(expected), &format)
        && format.freq > 0 && format.channels > 0;

    CNA::Internal::Audio::DestroyMixer();
    std::printf("CONCURRENT_FIRST_USE callers=%zu failures=%u sameMixer=%d validFormat=%d\n",
                kThreadCount, failures.load(std::memory_order_relaxed), sameMixer, validFormat);
    return sameMixer && validFormat ? 0 : 1;
}
